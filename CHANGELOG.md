# Changelog

## 1.4.1 — 2026-11-07

- Click or tap a top fan image during Post Print to cancel its current timer and immediately resume its normal static, printer-follow, or temperature-curve control. Synchronized fans cancel together; independent fans cancel separately.
- Preserved Post Print settings for future jobs, added keyboard-accessible image buttons, and prevented stale telemetry responses from restoring a canceled countdown.

## 1.4.0 — 2026-11-07

- Added a separate percentage mapping curve for each output following a printer fan. Existing configurations default to 1:1 following; temperature curves and follow curves are preserved independently across mode changes and restarts.
- Added an editable minimum running speed at 1% input, linear interpolation between points, and an immutable 0% → 0% point. For example, 1% → 35% and 100% → 100% lets an external exhaust fan start at 35% and ramp up with the printer fan.
- Replaced the shared fan chart and Fan 1/Fan 2 selector with separate graphs and point editors. Each graph uses printer fan percentage or nozzle temperature according to its saved mode; synchronized fans share one graph. A point picker makes closely spaced points easy to select on mobile.
- Added mapping validation, atomic persistence, backward-compatible HTTP updates, and regression coverage for migration, reboot, synchronization, failed saves, rounding, override precedence, and independent graph editing.
- Added an explicit release-version build option to start this feature series at 1.4.0; subsequent normal builds increment the final digit.

## 1.3.13 — 2026-11-07

- Fixed the web interface becoming unavailable when the printer is offline at startup or during reconnects. MQTT network operations now run in a background task so HTTP requests and fan control can continue.
- Fixed retry pacing to wait three seconds after a failed attempt, added transport timeouts, and reconnect with updated printer settings while discarding stale connection results.
- Added regression coverage for stalled connections and reads, printer recovery, configuration changes during connection, Wi-Fi loss, retry timing, and worker startup failures.

## 1.3.12 — 2026-10-23

- Made fan settings percentage fields compact for three digits and Post Print duration fields for four digits, with room for the number controls. Changed the duration label from "s" to "seconds".

## 1.3.11 — 2026-10-23

- Widened the Printer Information text fields and matched their input font size to the fan settings fields.

## 1.3.10 — 2026-10-23

- Widened Settings to match the Fan Graph panel on desktop, kept the mobile layout, and aligned the firmware footer with both panels.

## 1.3.9 — 2026-10-23

- Moved save notifications to the top right of the page.
- Updated Printer Information with matching label typography, right-aligned input fields, no trailing colons, and more space beneath the heading. Fields stack on narrow screens.

## 1.3.8 — 2026-10-23

- Matched the debugging switches' label font and row spacing to the fan controls.
- Added versioned OTA filenames such as `build/esp32dev/firmware-1.3.8.bin` to the build output.

## 1.3.7 — 2026-10-23

- Aligned settings switches on the left with their descriptions beside them and parameter controls on the right. Removed the Synchronize Fans On/Off labels.
- Simplified independent fan headings to **Fan 1** and **Fan 2**, and hid the fan heading in synchronized mode.

## 1.3.6 — 2026-10-23

- Added independent **Post Print** switches, durations in seconds, and speed percentages. Timers temporarily override static speed, printer fan following, or the temperature curve after a running print finishes, fails, or becomes idle.
- Added live Post Print countdowns below the fan percentages. Timers clear on reset or Wi-Fi/MQTT loss and do not restart from repeated end reports or an end state received after reconnecting.
- Preserve both fans' Post Print settings across synchronization changes and restarts; existing configurations default the feature to Off.
- Made the firmware version footer half its previous text size and left-aligned.
- Added timer, connection-loss, persistence, input-validation, and UI regression coverage.

## 1.2.5 — 2026-10-23

- Reserved an empty source-label line beneath fans that are not following, keeping the two fan images aligned.
- Matched Bambu Studio's 10% speed steps for part, auxiliary, and chamber fan reports, fixing cases where a printer setting of 70% appeared as 71%. Static speeds and temperature curves retain their existing precision.

## 1.2.4 — 2026-10-23

- Added **Follow Printer Fan** for each output, with Part Cooling, Auxiliary Cooling, Chamber/Exhaust, and Hotend/Heatbreak sources. Enabling it turns off Static Fan Speed, and enabling Static Fan Speed turns following off. Both can be off to use the temperature curve.
- Added a white source label below each following fan's speed indicator, and enlarged both override labels.
- Decode packed and separate MQTT fan-speed fields, retain speeds across partial updates and disconnects, and use 0% until the first valid report after boot.
- Persist follow settings independently for both outputs, preserve dormant Fan 2 settings during synchronization, and default following to Off for existing configurations.
- Added MQTT parsing, output control, persistence, invalid-input, and UI regression tests.

## 1.1.3 — 2026-10-23

- Removed the separate Fan control heading and renamed the switch label to **Synchronize Fans**, using heading-size text beside the switch.

## 1.1.1 — 2026-10-23

- Replaced the synchronization and curve selectors with graphical switches. Fan labels consistently use **Fan 1** and **Fan 2**, with white speed percentages for both indicators.
- Clicking a curve point now selects its fan and opens the point editor. Overlapping points prefer the currently selected fan.
- Replaced the save dialog with a notification that disappears after four seconds. Failed saves continue to show an error.
- Changed the displayed name on the web pages to **BL Smart Flow+**.
- Adopted major.feature.build versioning, with an automatic build-number increment and build date for each firmware build.

## 1.1.0 — 2026-10-23

- Added **Both fans synchronized**, defaulting to On for new and existing configurations.
- Added independent fan curves and static-speed overrides. Turning synchronization On uses Fan 1's settings for both outputs while preserving Fan 2's settings for later use.
- Added two labeled curves on one chart, a fan editing selector, separate feature sections with inline static-speed controls, and two independently animated fan indicators.
- Persisted both fans' static settings and curves, validated curve updates, and applied web changes only after a successful flash write.
- Added migration, GPIO output, save-failure, and UI regression tests. Invalid saved curves now fall back to defaults without deleting the configuration file.

## 1.0.3 — 2026-10-23

- Added a transparent favicon by overlaying `smartflow_blades.png` on `smartflow_shell.png`, with 16, 32, 48, and 64 pixel sizes.
- Embedded the favicon in the firmware, served it at `/favicon.ico`, and linked it from the HTML pages.
- Updated the build script to regenerate the favicon header alongside the compressed HTML headers.

## 1.0.2 — 2026-10-23

- Fixed blank settings fields by loading configuration immediately instead of waiting for external images to finish loading.
- Added a visible error when settings cannot be loaded and disabled Save until loading succeeds, preventing accidental overwrites with empty values.
- Stopped logging submitted credentials in the browser console.
- Added regression tests for settings loading, failed requests, and saving before settings arrive.

## 1.0.1 (retracted) — 2026-10-23

Retracted because the settings page could remain blank while waiting for external images to load. The saved configuration was still present; the loading problem was fixed in 1.0.2.

- Fixed web settings persistence so changes to the printer access code are written to flash and survive a restart.
- Confirmed a successful save only after writing the configuration, and returned an error when saving fails.
- Added temporary-file writes to protect the previous configuration against incomplete writes, and bounded JSON parsing to the configuration file's size.
- Updated the displayed firmware version and date, regenerated the HTML headers, and made future builds regenerate the page headers used by the web server.
