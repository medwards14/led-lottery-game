Linux kernel module LED lottery game for raspberry pi 5 with custom peripheral.
Instructions for use:
1. Create a project directory and pull the files into it.
2. Run 'make' to generate device tree blob file led_lottery.dtbo, as well as the module file led_lottery.ko to be inserted into the kernel.
3. Run 'sudo dtoverlay led_lottery.dtbo' to add the project to the device tree. Verify that it worked by running 'sudo ls /proc/device-tree/led_lottery_device/' and look for the following output: ''.
4. Open a separate bash terminal, and run the command 'dmesg -W' to see messages related to the game state.
5. Run 'sudo insmod led_lottery.ko' to insert the module. You should now see the message 'led_lottery: Probing device', and 'led_lottery: Driver initialized successfully' appear in the dmesg log.
6. To start the game, run the following command: 'echo "start" | sudo tee /proc/led_lottery'. The dmesg log should now show the following messages: 'led_lottery: Command received: start', 'led_lottery: Starting game...', and 'led_lottery: Game started.'.
7. A winning LED has been chosen at random. Now, you mut cycle through the LEDs by pressing the button, and when you have decided which one you think is the 'winning LED', run the command 'echo "guess" | sudo tee /proc/led_lottery' to make your decision. If you guessed correctly (which has a probability of 0.25), the following messages will appear: 'led_lottery: Correct guess! You win!', and 'led_lottery: Game reset after guess.'. If you guessed incorrectly (which has a 0.75 probability), you will get the alternative message: 'led_lottery: Incorrect guess! You lose.'.
8. To replay the game after making a guess, run the start command 'echo "guess" | sudo tee /proc/led_lottery" again and guess again.
9. When you are finished playing the game, unload the game module from the kernel by running the command 'sudo rmmod led_lottery', and remove the generated project files from the device tree by running the command 'sudo dtoverlay -r led_lottery'. These commands MUST be run after running the game.
