#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>

#define NUM_LEDS 4

// pointers defined for GPIO-based hardware access
static struct gpio_desc *leds[NUM_LEDS];
static struct gpio_desc *button;
static struct gpio_desc *buzzer;
static struct proc_dir_entry *proc_file;

// variables to track the game state
static int target_led;
static int current_led;
static int game_active;
static int button_irq;

/**
 * interrupt handler for button presses
 */
static irqreturn_t button_irq_handler(int irq, void *dev_id)
{
    pr_info("led_lottery: Button pressed (interrupt)\n");

    if (!game_active) {
        pr_info("led_lottery: No active game. Press 'start' to begin.\n");
        return IRQ_HANDLED;
    }

    // cycle to the next LED
    gpiod_set_value(leds[current_led], 0);
    current_led = (current_led + 1) % NUM_LEDS;
    gpiod_set_value(leds[current_led], 1);

    pr_info("led_lottery: Current LED is %d\n", current_led);
    return IRQ_HANDLED;
}

/**
 * write function for /proc/led_lottery to handle commands:
 *  - "start":  initializes the game, chooses a random LED.
 *  - "guess":  checks if the current LED matches the target.
 *  - "reset":  resets the game state and turns off LEDs/buzzer.
 */
static ssize_t led_lottery_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
    char command[16];
    pr_info("led_lottery: Write called\n");

    if (count > 15) {
        pr_warn("led_lottery: Command too long. Max length is 15 characters.\n");
        return -EINVAL;
    }
    if (copy_from_user(command, buf, count)) {
        pr_err("led_lottery: Failed to copy command from user space.\n");
        return -EFAULT;
    }

    command[count] = '\0';
    pr_info("led_lottery: Command received: %s\n", command);

    if (strncmp(command, "start", 5) == 0) {
        if (game_active) {
            pr_info("led_lottery: Game already active. Use 'reset' to restart.\n");
        } else {
            pr_info("led_lottery: Starting game...\n");
            // choose random LED as target
            target_led = get_random_u32() % NUM_LEDS;
            current_led = 0;
            game_active = 1;
            // light the first LED to start
            gpiod_set_value(leds[current_led], 1);
            pr_info("led_lottery: Game started. Target LED is %d (hidden).\n", target_led);
        }
    }
    else if (strncmp(command, "guess", 5) == 0) {
        if (!game_active) {
            pr_info("led_lottery: No active game. Press 'start' first.\n");
            return count;
        }
        // check if user guessed correctly
        if (current_led == target_led) {
            pr_info("led_lottery: Correct guess! You win!\n");
            gpiod_set_value(buzzer, 1);
            msleep(500);
            gpiod_set_value(buzzer, 0);
        } else {
            pr_info("led_lottery: Incorrect guess. Try again!\n");
        }

	// auto-reset after guess
        pr_info("led_lottery: Game reset after guess.\n");
        game_active = 0;
        for (int i = 0; i < NUM_LEDS; i++) {
            gpiod_set_value(leds[i], 0);
        }
        gpiod_set_value(buzzer, 0);
    }
    else if (strncmp(command, "reset", 5) == 0) {
        pr_info("led_lottery: Resetting game.\n");
        game_active = 0;
        for (int i = 0; i < NUM_LEDS; i++) {
            gpiod_set_value(leds[i], 0);
        }
        gpiod_set_value(buzzer, 0);
        pr_info("led_lottery: Game reset. Press 'start' to begin.\n");
    }
    else {
        pr_warn("led_lottery: Unknown command: %s\n", command);
    }
    return count;
}

static struct proc_ops proc_fops = {
    .proc_write = led_lottery_write,
};

/**
 * probe function: called when the driver is loaded
 */
static int led_lottery_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    int ret;

    pr_info("led_lottery: Probing device\n");

    // using leds[] array to request LEDs based on names from dtoverlay file
    leds[0] = gpiod_get(dev, "red-led", GPIOD_OUT_LOW);
    leds[1] = gpiod_get(dev, "blue-led", GPIOD_OUT_LOW);
    leds[2] = gpiod_get(dev, "green-led", GPIOD_OUT_LOW);
    leds[3] = gpiod_get(dev, "yellow-led", GPIOD_OUT_LOW);

    for (int i = 0; i < NUM_LEDS; i++) {
        if (IS_ERR(leds[i])) {
            pr_err("led_lottery: Failed to request LED %d: %ld\n", i, PTR_ERR(leds[i]));
            return PTR_ERR(leds[i]);
        }
    }

    // using button* to request button GPIO
    button = gpiod_get(dev, "button", GPIOD_IN);
    if (IS_ERR(button)) {
        pr_err("led_lottery: Failed to request button GPIO: %ld\n", PTR_ERR(button));
        return PTR_ERR(button);
    }

    // setting up button interrupt
    button_irq = gpiod_to_irq(button);
    if (button_irq < 0) {
        pr_err("led_lottery: Failed to get button IRQ: %d\n", button_irq);
        return button_irq;
    }
    ret = request_irq(button_irq, button_irq_handler, IRQF_TRIGGER_FALLING, "led_lottery_button", NULL);
    if (ret) {
        pr_err("led_lottery: Failed to request IRQ for button\n");
        return -EBUSY;
    }

    // using buzzer* to request buzzer
    buzzer = gpiod_get(dev, "buzzer", GPIOD_OUT_LOW);
    if (IS_ERR(buzzer)) {
        pr_err("led_lottery: Failed to request buzzer GPIO: %ld\n", PTR_ERR(buzzer));
        return PTR_ERR(buzzer);
    }

    // creating /proc/led_lottery file
    proc_file = proc_create("led_lottery", 0666, NULL, &proc_fops);
    if (!proc_file) {
        pr_err("led_lottery: Failed to create /proc/led_lottery\n");
        return -ENOMEM;
    }

    pr_info("led_lottery: Driver initialized successfully\n");
    return 0;
}

/**
 * remove function: cleans up when the driver is unloaded
 */
static int led_lottery_remove(struct platform_device *pdev)
{
    free_irq(button_irq, NULL);
    proc_remove(proc_file);

    for (int i = 0; i < NUM_LEDS; i++) {
        gpiod_set_value(leds[i], 0);
        gpiod_put(leds[i]);
    }
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

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LED Lottery Game Driver with Interrupt Support");
MODULE_AUTHOR("Mason Edwards");

module_platform_driver(led_lottery_driver);

