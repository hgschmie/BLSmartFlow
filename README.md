## BLSMARTFLOW

The BLSmartflow is an ESP32 based device that connects to your Bambulab X1,X1C,P1P Or P1S and controls fans based on the temperature of the printer.

### Features

- Connects to Bambulab H2D,X1,X1C,P1P Or P1S
- Controls Fans based on printer temperature

### BL Smart Flow+ Features

- Synchronized or independent control of both fans
- Separate graphs for each fan, using nozzle temperature or printer fan percentage
- Per-fan static speed overrides
- Per-fan printer fan following with adjustable speed mapping and a minimum starting speed
- Per-fan Post Print timers with a chosen duration and speed
- Separate animated fan indicators
- Live printer connection status with a retry countdown

### Changelog

See [CHANGELOG.md](CHANGELOG.md) for the release history.

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

### OTA Updates with curl

From the project folder, upload the versioned firmware image to the controller. Replace `CONTROLLER_IP` with the controller's IP address and adjust the filename for the version you want to install:

```bash
curl --fail --show-error \
  -F "update=@build/esp32dev/firmware-1.5.0.bin" \
  "http://CONTROLLER_IP/update"
```

Use `firmware-<version>.bin` for OTA uploads; the merged image is for flashing over USB. Check the response: `OK` means the update succeeded and the controller will reboot; `FAIL` means the update failed.

When upgrading from firmware earlier than 1.3.13, keep the printer powered on and reachable during the upload so the old firmware's MQTT connection attempts do not block the web server.

### Setup Instructions
Once you have uploaded the firmware to your device, please visit the [dutchdevelop.com/blsmartflow](https://www.dutchdevelop.com/blsmartflow) website for detailed setup instructions.


### Development Notes

#### Firmware Versioning

Versions use **major.feature.build**: the first number changes for a major overhaul, the middle number for feature additions, and the last number increments for every firmware build.

`version_build.py` reserves the next build number in `platformio.ini` before compiling, including when Upload builds new firmware. A failed build still consumes its number. Cleaning, IDE metadata, filesystem-only operations, and uploading an existing binary with `nobuild` do not increment it. Commit the updated `custom_version` with the changes being built. To start a feature series at an exact release version, run `BLSF_RELEASE_VERSION=1.5.0 pio run -e esp32dev`. This records and builds 1.5.0; subsequent normal builds increment to 1.5.1, 1.5.2, and so on. Use the release override only when intentionally reserving a specific version.

The compiler and `.firmware` output filenames use the same reserved version. The displayed date comes from the build machine's local date. Web pages load the version from the firmware instead of keeping a separate hardcoded copy.

Each build also writes the OTA-ready image to `build/esp32dev/firmware-<version>.bin`, for example `firmware-1.3.8.bin`. Use this versioned file when uploading through the web UI. PlatformIO's internal `firmware.bin` remains available for its upload and merge steps.

#### Independent Fan Control

**Synchronize Fans: On** uses Fan 1's settings and curves for both outputs. This is also the default after upgrading from 1.0.x. Fan 1 is GPIO 17; Fan 2 is GPIO 16.

Set synchronization to **Off** and save to configure the fans separately. Fan 2 initially copies Fan 1's current settings. The Fan Graph page shows a separate graph and point editor for **Fan 1** and **Fan 2**. A fan with Follow Printer Fan enabled uses **Printer fan (%)** as its input; otherwise its graph uses **Nozzle temperature (°C)**. Synchronized mode shows one graph for both fans. Click a point or use **Select point** to open its editor, then press **Change**, **Add point**, or **Remove** to save that change. Confirmation disappears after four seconds. Each fan's static-speed switch overrides its own curve with the entered percentage.

Switching synchronization back On preserves Fan 2's settings and both curve types, including across restarts. Switching Off again restores them. Configuration changes take effect after a successful Save; save synchronization and follow-mode changes before editing curves.

The existing HTTP endpoints remain available: `points` and `speed` still describe Fan 1. `/getFanConfig` adds `points2` and `fansSynchronized`; `/sensorData` adds `speed2`. To update a specific curve, POST `fan=1` or `fan=2` to `/updateFanConfig` alongside the existing `points` form field containing `{"points":[{"temp":0,"speed":0},...]}`. Omitting `fan` selects Fan 1.

#### Follow Printer Fan

Below each **Static Fan Speed** row, enable **Follow Printer Fan**, choose a source, and save. By default the output mirrors that printer fan's reported percentage; its mapping curve can translate the reported speed into a different output speed. The two switches are mutually exclusive, and either can always be switched off. When both are off, the output uses its temperature curve. **Synchronize Fans: On** applies Fan 1's settings to both outputs; switching it Off restores Fan 2's saved settings. The selected source appears in white below the speed indicator after a successful save.

On the **Fan Graph** page, edit that fan's percentage curve. For an external exhaust fan that needs 35% to start, select the **1% → 1%** point, change its output to **35%**, and keep the **100% → 100%** point. The result is 0% → 0%, 1% → 35%, 50% → 67%, 70% → 80%, and 100% → 100%.

The 0% point is fixed at 0% so a stopped printer fan always stops the following output unless Static Fan Speed or Post Print is overriding it. Inputs at 1% and 100% stay fixed, with editable output percentages; add intermediate points from 2–99% for other shapes. Inputs and outputs use whole percentages, with linear interpolation rounded to the nearest whole output percentage. Each fan stores its temperature curve and percentage curve separately, so switching modes preserves both. Existing configurations retain their settings and receive a 1:1 percentage curve.

| Source | MQTT field | Indicator label |
| --- | --- | --- |
| Part Cooling Fan | `print.cooling_fan_speed` | Part Cooling Fan |
| Auxiliary Cooling Fan | `print.big_fan1_speed` | Auxiliary Fan |
| Chamber/Exhaust Fan | `print.big_fan2_speed` | Chamber Fan |
| Hotend/Heatbreak Fan | `print.heatbreak_fan_speed` | Hotend Fan |

The packed `print.fan_gear` field contains three 0–255 values (part, auxiliary, chamber) and takes precedence when present. Those values are rounded to the nearest 10% step to match [Bambu Studio's fan display](https://github.com/bambulab/BambuStudio/blob/master/src/slic3r/GUI/Widgets/FanControl.cpp). For example, PWM 180 becomes 70%, not 71%. Separate 0–15 fields for these three fans use `floor(raw / 1.5) * 10`, matching [Bambu Studio's decoder](https://github.com/bambulab/BambuStudio/blob/master/src/slic3r/GUI/DeviceCore/DevFan.cpp). Hotend/heatbreak readings use a separate 0–15 scale and are converted to the nearest whole percentage. These are reported speed levels, not RPM measurements; a source is usable only if the printer reports it. Static speeds and curve speeds are not rounded to 10% steps.

Each source starts at **0% after boot** and keeps its last valid speed when MQTT disconnects, a field is omitted, or a value is malformed. An explicitly reported zero stops the following output. Speeds are kept in RAM; the follow settings and mapping curves are saved to flash. Configurations predating the following feature load with following Off.

`/getOptions` and `/submitOptions` add `followPrinterFan` and `printerFanSource` (and `followPrinterFan2` / `printerFanSource2`). Sources are `part`, `auxiliary`, `chamber`, or `heatbreak`. The form posts explicit `on`/`off` values; requests enabling both overrides are rejected. `/sensorData` adds `printerFans`, containing each source's last percentage or `null` before its first report.

`/getFanConfig` also returns `followPoints` and `followPoints2`, with points shaped as `{"input":1,"speed":35}`. To update one, POST `mode=follow`, `fan=1` or `fan=2`, and a `points` form field such as `{"points":[{"input":0,"speed":0},{"input":1,"speed":35},{"input":100,"speed":100}]}` to `/updateFanConfig`. `mode=temperature` (or omitting `mode`) continues to edit the temperature curve using `temp` keys.

#### Post Print

Enable **Post Print** below **Follow Printer Fan**, enter a duration in seconds (1–86400) and a speed (0–100%), and save. For example, **600 s at 100%** runs for ten minutes. Post Print can be enabled alongside either normal override; it does not change the Static Fan Speed or Follow Printer Fan switches.

After observing `RUNNING` on the current MQTT connection, the next `FINISH`, `FAILED`, or `IDLE` report starts the timer. Partial updates and intervening `PREPARE` or `PAUSE` reports preserve that observation. Repeated end reports do not restart it. `PREPARE`, `RUNNING`, and `PAUSE` do not cancel or alter an already running countdown.

During the timer, its speed takes priority over the normal mode, and **Post Print: 600s** (with the remaining seconds) replaces the source label beneath the fan percentage. Telemetry refreshes every second. When the timer expires, the fan returns to its current static speed, mapped last reported printer-fan speed, or temperature curve. A reset or detected Wi-Fi/MQTT disconnect clears both timers and the previous running observation; reconnecting to an end state cannot start an old timer. Only settings are stored in flash.

Click or tap the fan image at the top to cancel its current Post Print timer early. The image is also a keyboard-accessible button while a timer is running. Cancellation immediately resumes the normal control mode, which may still request a nonzero speed. Independent fans cancel separately; synchronized fans cancel together. Post Print stays enabled for future prints, and no settings are written to flash. The HTTP equivalent is `POST /cancelPostPrint` with `fan=1` or `fan=2`; the response contains updated sensor data and output speeds.

**Synchronize Fans: On** applies Fan 1's Post Print settings and countdown to both outputs, retaining Fan 2's settings for later. Changing synchronization cancels active timers. Saving a change to a fan's Post Print settings cancels its current timer; failed saves leave it running. Older configurations default Post Print to Off, with editable defaults of 600 seconds and 100%.

`/getOptions` and `/submitOptions` use `postPrint`, `postPrintSeconds`, and `postPrintSpeed`, with a `2` suffix for Fan 2. `/sensorData` returns `postPrintRemaining` and `postPrintRemaining2` as whole seconds rounded up, or zero when inactive. Synchronized outputs report the same remaining time.

#### MQTT Availability

The web interface and fan control remain available when the printer is off or unreachable. MQTT connection, subscription, reads, and disconnects run in a background task. Failed connections wait three seconds after completion before retrying; TCP/TLS and MQTT response waits also have timeouts.

The indicator beneath the fan images shows **Connected**, **Connecting…**, or **Not connected, retrying in N seconds** on both tabs. Its countdown comes from the controller's retry schedule and refreshes every second. Missing printer settings, controller Wi-Fi loss, and reconnecting have separate messages. If a status request fails or no fresh status arrives for five seconds, the page shows **Status unavailable (controller not responding)**.

`/sensorData` includes `printerConnection` with `state` (`connected`, `connecting`, `retrying`, `reconnecting`, `wifi_disconnected`, or `not_configured`) and `retryInSeconds`. Retry seconds are rounded up from the remaining delay; zero means the next attempt is due. The field is zero outside the retrying state.

Saving a different printer IP, access code, or serial number closes the old connection and reconnects with the saved settings. Results from an attempt interrupted by a Wi-Fi loss or settings change are discarded. Printer reports and configuration changes are applied in the main loop so that Post Print timers and fan settings cannot be updated concurrently by the network task.

#### Tests

After a PlatformIO build has installed dependencies, run:

```sh
node --test test/settings-page.test.js
python3 test/version_build_test.py
python3 test/run-host-tests.py
```

The native tests use the production firmware handlers and ArduinoJson with simulated flash, HTTP, and GPIO I/O, plus address and undefined-behavior sanitizers. MQTT tests run the actual background task on a host thread with simulated blocking network operations and queues. Physical fan operation and a power-cycle check should also be verified on the device after an OTA update.

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
