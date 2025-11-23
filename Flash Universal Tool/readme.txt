# SPI Flash Diagnostic Tool

## Project Description
This project acts as a diagnostic tool for SPI Flash memory chips using a Raspberry Pi Pico. It allows for identifying chips via JEDEC ID, reading/writing/erasing sectors, and reporting data remotely via MQTT.

## Folder Structure
* **/src**: Contains the source code implementation.
    * `main.c`: Entry point and main system loop.
    * `mqtt.c`: Handles network connection and publishing.
    * `spi_ops.c`: Low-level hardware SPI driver.
* **/include**: Contains header files and public API definitions.
* **/lib**: External libraries (FatFS for SD card support).
