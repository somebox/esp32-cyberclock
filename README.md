# ESP32 Cyberclock

![cyberclock](./docs/cyberclock-hero.gif)
![cyberclock](./docs/cyberclock-closeup.gif)

I decided one day to make a clock using old-school 2.3" LED 7-segment displays. Which I happen to have a lot of. After hacking on it for a few weekends, it ended up being this:

- 7 circuit boards and three custom mounted sections, mounted to an aluminum frame, about 600mm wide
- An ESP32 WROOM32 devkit board
- custom power board PCB for the ESP32, which provides a DC high voltage rail, 5v, 3.3v, level shifting (76hc125n) and various breakout headers
- 6x 2.3" 7-segment displays
- each digit is mounted on a custom PCB with a 74hc596c driver IC and current-limiting resistors
- WS2812b 5050 LEDs are used for the circular effect (a common pbc, with 3d-printed mount), and the custom separator dot panels, all of which are mounted with hand-machined aluminum sections
- An I2C ambient light meter, so the clock can adjust brightness automatically
- A small OLED display shows the time, date, IP address, wifi strength and light level (lux)
- The clock is powered with 9v DC wall plug, which gets stepped down to around 7v (for the 2.3" digits), and then 5v for the ESP32 board and LEDs.

## The Code

The code for this clock has been a journey. There are a lot of things going on at the same time, animations at different speeds, and periodic changes. At first, I was hacking this counting interations and tweaking, but it soon became clear this was causing problems. This led to some frustrated rewrites and a lot of learning.

I decided to give [NeoPixelBus](https://github.com/Makuna/NeoPixelBus) a try, which I had been using for basic things, and discovered the [NeoPixelAnimator](https://github.com/Makuna/NeoPixelBus/wiki/NeoPixelAnimator-object) object. Unline other periodic timer libraries, NeoPixelAnimator can manage multiple repeating animation cycles, and uses call backs with progress indication. It also has built-in easing functions, which helps make things feel more organic. 

This repository contains the firmware for the clock as well as various pictures, drawings and schematics, in case you want to make something similar.

## Schematics

### The dual-rail power board with level shifting
![cyberclock](./docs/esp32-power-board.png)

### The LED 7-segment digit driver board (common anode, 74HC596c)
![cyberclock](./docs/led-driver-board.png)

## Some Pictures

![cyberclock](./docs/cyberclock-floor.jpeg)
![cyberclock](./docs/oled-closeup.jpeg)
![cyberclock](./docs/control-board.jpeg)
![cyberclock](./docs/rear-view.jpeg)
