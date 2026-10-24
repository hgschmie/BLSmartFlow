## BLSMARTFLOW

The BLSmartflow is an ESP32 based device that connects to your Bambulab X1,X1C,P1P Or P1S and controls fans based on the temperature of the printer.

### Features

- Connects to Bambulab H2D,X1,X1C,P1P Or P1S
- Controls Fans based on printer temperature
- Drives two fans together or independently, with separate curves and static-speed overrides

### Changelog

#### 1.1.3 — 2026-10-23

- Removed the separate Fan control heading and renamed the switch label to **Synchronize Fans**, using heading-size text beside the switch.

#### 1.1.1 — 2026-10-23

- Replaced the synchronization and curve selectors with graphical switches. Fan labels consistently use **Fan 1** and **Fan 2**, with white speed percentages for both indicators.
- Clicking a curve point now selects its fan and opens the point editor. Overlapping points prefer the currently selected fan.
- Replaced the save dialog with a notification that disappears after four seconds. Failed saves continue to show an error.
- Changed the displayed name on the web pages to **BL Smart Flow+**.
- Adopted major.feature.build versioning, with an automatic build-number increment and build date for each firmware build.

#### 1.1.0 — 2026-10-23

- Added **Both fans synchronized**, defaulting to On for new and existing configurations.
- Added independent fan curves and static-speed overrides. Turning synchronization On uses Fan 1's settings for both outputs while preserving Fan 2's settings for later use.
- Added two labeled curves on one chart, a fan editing selector, separate feature sections with inline static-speed controls, and two independently animated fan indicators.
- Persisted both fans' static settings and curves, validated curve updates, and applied web changes only after a successful flash write.
- Added migration, GPIO output, save-failure, and UI regression tests. Invalid saved curves now fall back to defaults without deleting the configuration file.

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

#### Firmware Versioning

Versions use **major.feature.build**: the first number changes for a major overhaul, the middle number for feature additions, and the last number increments for every firmware build.

`version_build.py` reserves the next build number in `platformio.ini` before compiling, including when Upload builds new firmware. A failed build still consumes its number. Cleaning, IDE metadata, filesystem-only operations, and uploading an existing binary with `nobuild` do not increment it. Commit the updated `custom_version` with the changes being built. When starting a new feature series or major version, update the corresponding number in `custom_version`; the next build increments its final number as usual.

The compiler and `.firmware` output filenames use the same reserved version. The displayed date comes from the build machine's local date. Web pages load the version from the firmware instead of keeping a separate hardcoded copy.

#### Independent Fan Control

**Synchronize Fans: On** uses Fan 1's curve and static-speed override for both outputs. This is also the default after upgrading from 1.0.x. Fan 1 is GPIO 17; Fan 2 is GPIO 16. Both curves use the printer's nozzle temperature.

Set synchronization to **Off** and save to configure the fans separately. Fan 2 initially copies Fan 1's current settings. The Fan Graph page shows both curves; the **Edit curve** switch selects **Fan 1** on the left and **Fan 2** on the right. Clicking a point also selects its fan and opens the editor; overlapping points prefer the currently selected fan. Valid point changes save automatically, with a confirmation that disappears after four seconds. Each fan's static-speed switch overrides its own curve with the entered percentage.

Switching synchronization back On preserves Fan 2's curve and static settings, including across restarts. Switching Off again restores them. Configuration changes take effect after a successful Save; unsaved changes to the synchronization selector do not change the running fan mode.

The existing HTTP endpoints remain available: `points` and `speed` still describe Fan 1. `/getFanConfig` adds `points2` and `fansSynchronized`; `/sensorData` adds `speed2`. To update a specific curve, POST `fan=1` or `fan=2` to `/updateFanConfig` alongside the existing `points` form field containing `{"points":[{"temp":0,"speed":0},...]}`. Omitting `fan` selects Fan 1.

#### Tests

After a PlatformIO build has installed dependencies, run:

```sh
node --test test/settings-page.test.js
python3 test/version_build_test.py
python3 test/run-host-tests.py
```

The native tests use the production firmware handlers and ArduinoJson with simulated flash, HTTP, and GPIO I/O, plus address and undefined-behavior sanitizers. Physical fan operation and a power-cycle check should also be verified on the device after an OTA update.

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
