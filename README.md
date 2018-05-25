# Nexboot - Bootloader for ESP32 developement boards

I was unable to find any bootloader for ESP32 doing what i would like to have, so, 
i started to write my own.

### How to install
 * install and configure esp-ipf and the esp32 toolkit
 * git clone the whole repository and subrepositories under your own project dir
 * make menuconfig to configure nexboot
 * erase the flash in your board ( suggested, not really needed )
 * build and upload as usual with make and make flash on your esp32 developement board.
 
### Assumptions:
 
 * you are updating your esp32 developement board.
 * your board have 4MB of flash size, or you will have to change the partitions.csv file 
 * you know what you are doing
 * No, it isn't finished and ready to use in production.
 * Yes, i will continue to work on it to improve it and eventually make it production ready.
 * No, i will not fix your computer

### Features
 * update OTA over WiFi, in AP or STA mode, wpa2 supported.
 * update OTA via web browser, on the ESP32 board IP
 * Force bootloader mode on boot by activating GPIO26
 * It leave you 3MB of space for your OTA firmware ( with a 4MB flash )
 
### Planned
 * HTTPS instead of HTTP ( configurable )
 * Authenticate login on HTTP/s
 * Firmware encryption
 * Firmware hash pre-verification
 * Better HTML/JS
 
### Status
 
Fairly limited atm, but it works. Really need a better interface and a lot of other things.
