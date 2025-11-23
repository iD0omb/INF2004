[SPI Flash Diagnostic Tool]

[Project Description]
This project acts as a diagnostic tool for SPI Flash memory chips using a Raspberry Pi Pico. It allows for identifying chips via JEDEC ID, reading/writing/erasing sectors, and reporting data remotely via MQTT. Also includes a WEBGUI.

[Folder Structure]
/src: Contains the source code implementation.
main.c: Entry point and main system loop.
mqtt.c: Handles network connection and publishing.
spi_ops.c: Low-level hardware SPI driver.
cli.c : Main Menu
json.c : Json formatting
sd_card.c : SD Card functions and initialization
web_server.c : webpage hosting and html generation
flash_ops.c : for destructive operations
flash_db.c : simple database struct for common chips
/include: Contains header files and public API definitions.
/lib: External libraries (FatFS for SD card support).

##################################################################################################
[How to compile and run]
---------------------------------------
Step 1: Install the Official Extension
---------------------------------------
Open VS Code.

Go to the Extensions.

Search for "Raspberry Pi Pico".

Install the extension by Raspberry Pi.

Note: Using this extension typically requires you to disable or uninstall the generic "CMake Tools" extension to avoid conflicts, or configure them to play nicely together.
---------------------------------------
Step 2: Allow Toolchain Setup
---------------------------------------
Once installed, the extension will likely prompt you to install the Raspberry Pi Pico SDK and Toolchain (Compiler, Ninja, etc.).

Click Yes or Install to let it handle the downloads.

Wait for the setup to complete.
---------------------------------------
Step 3: Open the Pico Project Dashboard
---------------------------------------
Look at the Activity Bar in VS Code.

Click the new Raspberry Pi Pico icon.

This opens the "Raspberry Pi Pico Project" side panel.
---------------------------------------
Step 4: Run the Import Wizard
---------------------------------------
In the Pico side panel, look for a section or button labeled Import Project

If you do not see a direct "Import" button, use the Command Palette:

Press Ctrl+Shift+P.

Type "Raspberry Pi Pico: Import Project" and select it.

A wizard window will appear.
---------------------------------------
Step 5: Configure the Import
---------------------------------------
Browse to the root folder (Flash Universal Tool).

SDK Version: Select the SDK version 2.2.0.

Click Import.
---------------------------------------
Step 6: Build and Flash
---------------------------------------
Once imported

Compile Project.

Run Project.

Click Compile Project.

To flash, hold the BOOTSEL button on your Pico, plug it in, and click Run Project.
---------------------------------------