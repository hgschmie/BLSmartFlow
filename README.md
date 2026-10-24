## BLSMARTFLOW

The BLSmartflow is an ESP32 based device that connects to your Bambulab X1,X1C,P1P Or P1S and controls fans based on the temperature of the printer.

### Features

- Connects to Bambulab H2D,X1,X1C,P1P Or P1S
- Controls Fans based on printer temperature

### Changelog

#### 1.0.3 — 2026-10-23

- Added a transparent favicon by overlaying `smartflow_blades.png` on `smartflow_shell.png`, with 16, 32, 48, and 64 pixel sizes.
- Embedded the favicon in the firmware, served it at `/favicon.ico`, and linked it from the HTML pages.
- Updated the build script to regenerate the favicon header alongside the compressed HTML headers.

#### 1.0.2 — 2026-10-23

- Fixed blank settings fields by loading configuration immediately instead of waiting for external images to finish loading.
- Added a visible error when settings cannot be loaded and disabled Save until loading succeeds, preventing accidental overwrites with empty values.
- Stopped logging submitted credentials in the browser console.
- Added regression tests for settings loading, failed requests, and saving before settings arrive.

#### 1.0.1 (retracted) — 2026-10-23

Retracted because the settings page could remain blank while waiting for external images to load. The saved configuration was still present; the loading problem was fixed in 1.0.2.

- Fixed web settings persistence so changes to the printer access code are written to flash and survive a restart.
- Confirmed a successful save only after writing the configuration, and returned an error when saving fails.
- Added temporary-file writes to protect the previous configuration against incomplete writes, and bounded JSON parsing to the configuration file's size.
- Updated the displayed firmware version and date, regenerated the HTML headers, and made future builds regenerate the page headers used by the web server.

### Development Environment

To contribute to the Smartflow project, you'll need the following tools:

### Tools & Libraries Used

- [Visual Studio Code](https://code.visualstudio.com/): A lightweight and powerful source code editor.
- [PlatformIO](https://platformio.org/): An open-source ecosystem for IoT development.
- [Python](https://www.python.org/): A programming language used for scripting and automation.
- [qpdf](https://qpdf.sourceforge.io/): A command-line tool and library of compression tools (`gzip`)

### Building and Running the Project
1. Clone the repository to your local machine.
2. Open the project folder in Visual Studio Code.
3. Ensure that PlatformIO is installed and configured in your Visual Studio Code environment.
4. Connect your Smartflow device ESP32 to your computer.
6. Build the project by clicking on the PlatformIO icon in the sidebar and selecting "Build" from the available options.
7. Once the build process is complete, upload the firmware to your device using the "Upload" option in PlatformIO.
8. After uploading the firmware, your BLSmartflow device should be ready to use.

### Setup Instructions
Once you have uploaded the firmware to your device, please visit the [dutchdevelop.com/blsmartflow](https://www.dutchdevelop.com/blsmartflow) website for detailed setup instructions.


### Development Notes

#### Generating .h Files for Compressed HTML

In embedded applications, HTML content is efficiently stored in PROGMEM memory. To achieve this, .h files are generated from compressed HTML files for webpages (i.e., `src/www/setuppage.html`) that are run on the device.

- `pre_build.py` runs automatically through `platformio.ini`, compressing web assets into `src/www/www.h` and regenerating the individual `fanpage.h`, `updatePage.h`, and `favicon.h` headers used by the web server.
- Rebuild after editing HTML or the favicon so the generated headers and firmware contain the updated assets.

The favicon overlays the fan blades on the shell, preserving transparency. To regenerate its 16, 32, 48, and 64 pixel sizes with ImageMagick, run:

```sh
magick src/www/smartflow_shell.png src/www/smartflow_blades.png -compose over -composite -define icon:auto-resize=64,48,32,16 src/www/favicon.ico
```

### License

The BLSmartflow is released under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0) license. See the [LICENSE](https://github.com/DutchDevelop/BLSmartFlow/blob/main/LICENSE) file for more details.

### Credits
- **[DutchDeveloper](https://dutchdevelop.com/)**: Lead programmer
- **[xps3riments](https://github.com/xps3riments)**: Inspiration for the foundation of the code
- **[longrackslabs](https://github.com/longrackslabs)**: Build process, documentation, developer & community support

### Author

This project was created by [DutchDeveloper](https://dutchdevelop.com/).
