#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/random.h> // For random number generation

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mason Edwards");
MODULE_DESCRIPTION("LED Lottery Game Driver with One-Chance Logic");
MODULE_VERSION("2.6");

#define NUM_LEDS 4
static struct gpio_desc *leds[NUM_LEDS];
static struct gpio_desc *button;
static struct gpio_desc *buzzer;
static struct proc_dir_entry *proc_file;

static unsigned int target_led; // Target LED for the lottery
static int current_led;         // Current LED index during cycling
static int game_active;         // Game active state
static int button_irq;          // Button IRQ number

static irqreturn_t button_irq_handler(int irq, void *dev_id) {
    pr_info("led_lottery: Button pressed (interrupt)\n");

    if (!game_active) {
        pr_info("led_lottery: No active game. Press 'start' to begin.\n");
        return IRQ_HANDLED;
    }

    gpiod_set_value(leds[current_led], 0); // Turn off current LED
    current_led = (current_led + 1) % NUM_LEDS;
    gpiod_set_value(leds[current_led], 1); // Turn on next LED

    pr_info("led_lottery: Current LED is %d\n", current_led);
    return IRQ_HANDLED;
}

static ssize_t led_lottery_write(struct file *file, const char __user *buf, size_t count, loff_t *off) {
    char command[16];
    if (count > 15) return -EINVAL;
    if (copy_from_user(command, buf, count)) return -EFAULT;

    command[count] = '\0';
    pr_info("led_lottery: Command received: %s\n", command);

    if (strncmp(command, "start", 5) == 0) {
        if (game_active) {
            pr_info("led_lottery: Game already active. Use 'reset' to restart.\n");
        } else {
            pr_info("led_lottery: Starting game...\n");

            // Randomize the target LED
            get_random_bytes(&target_led, sizeof(target_led));
            target_led = target_led % NUM_LEDS;

            current_led = 0;
            game_active = 1;
            gpiod_set_value(leds[current_led], 1); // Start with the first LED
            // pr_info("led_lottery: Game started. Target LED is %u (hidden).\n", target_led);
	    pr_info("led_lottery: Game started.\n");

        }
    } else if (strncmp(command, "guess", 5) == 0) {
        if (!game_active) {
            pr_info("led_lottery: No active game. Press 'start' to begin.\n");
            return count;
        }

        if (current_led == target_led) {
            pr_info("led_lottery: Correct guess! You win!\n");
            gpiod_set_value(buzzer, 1);
            msleep(500);
            gpiod_set_value(buzzer, 0);
        } else {
            pr_info("led_lottery: Incorrect guess! You lose.\n");
        }

        // Reset game state after the guess
        game_active = 0;
        for (int i = 0; i < NUM_LEDS; i++) {
            gpiod_set_value(leds[i], 0); // Turn off all LEDs
        }
        gpiod_set_value(buzzer, 0); // Ensure buzzer is off
        pr_info("led_lottery: Game reset after guess.\n");
    } else if (strncmp(command, "reset", 5) == 0) {
        game_active = 0;
        for (int i = 0; i < NUM_LEDS; i++) {
            gpiod_set_value(leds[i], 0); // Turn off all LEDs
        }
        gpiod_set_value(buzzer, 0); // Ensure buzzer is off
        pr_info("led_lottery: Game reset.\n");
    } else {
        pr_warn("led_lottery: Unknown command: %s\n", command);
    }

    return count;
}

static struct proc_ops proc_fops = {
    .proc_write = led_lottery_write,
};

static int led_lottery_probe(struct platform_device *pdev) {
    struct device *dev = &pdev->dev;
    pr_info("led_lottery: Probing device\n");

    leds[0] = gpiod_get(dev, "red-led", GPIOD_OUT_LOW);
    leds[1] = gpiod_get(dev, "blue-led", GPIOD_OUT_LOW);
    leds[2] = gpiod_get(dev, "green-led", GPIOD_OUT_LOW);
    leds[3] = gpiod_get(dev, "yellow-led", GPIOD_OUT_LOW);

    for (int i = 0; i < NUM_LEDS; i++) {
        if (IS_ERR(leds[i])) return PTR_ERR(leds[i]);
    }

    button = gpiod_get(dev, "button", GPIOD_IN);
    if (IS_ERR(button)) return PTR_ERR(button);

    button_irq = gpiod_to_irq(button);
    if (button_irq < 0) return button_irq;

    if (request_irq(button_irq, button_irq_handler, IRQF_TRIGGER_FALLING, "led_lottery_button", NULL)) {
        return -EBUSY;
    }

    buzzer = gpiod_get(dev, "buzzer", GPIOD_OUT_LOW);
    if (IS_ERR(buzzer)) return PTR_ERR(buzzer);

    proc_file = proc_create("led_lottery", 0666, NULL, &proc_fops);
    if (!proc_file) return -ENOMEM;

    pr_info("led_lottery: Driver initialized successfully\n");
    return 0;
}

static int led_lottery_remove(struct platform_device *pdev) {
    free_irq(button_irq, NULL);
    proc_remove(proc_file);
    for (int i = 0; i < NUM_LEDS; i++) gpiod_put(leds[i]);
    gpiod_put(button);
    gpiod_put(buzzer);
    pr_info("led_lottery: Driver removed\n");
    return 0;
}

static const struct of_device_id led_lottery_of_match[] = {
    { .compatible = "custom,led-lottery" },
    {},
};
MODULE_DEVICE_TABLE(of, led_lottery_of_match);

static struct platform_driver led_lottery_driver = {
    .probe = led_lottery_probe,
    .remove = led_lottery_remove,
    .driver = {
        .name = "led_lottery_driver",
        .of_match_table = led_lottery_of_match,
    },
};

module_platform_driver(led_lottery_driver);
