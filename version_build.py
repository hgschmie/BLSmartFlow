"""Reserve a new major.feature.build version for each firmware build."""
from datetime import date
from pathlib import Path
import re


def configure_version(env, targets):
    config_path = Path(env.subst("$PROJECT_CONFIG"))
    config = config_path.read_text()
    section = re.search(
        r"(?m)^\[env:" + re.escape(env["PIOENV"]) + r"\][^\n]*\n(?:(?!^\[)[\s\S])*",
        config,
    )
    if not section:
        raise ValueError("Missing firmware environment in platformio.ini")
    match = re.search(r"(?m)^(custom_version\s*=\s*)(\d+)\.(\d+)\.(\d+)\b", section.group())
    if not match:
        raise ValueError("custom_version must use major.feature.build format")
    major, feature, build = map(int, match.group(2, 3, 4))
    non_firmware_targets = {
        "clean", "cleanall", "idedata", "__idedata", "envdump", "nobuild",
        "buildfs", "uploadfs", "uploadfsota", "erase", "compiledb", "sizedata",
    }
    if not env.IsCleanTarget() and not env.IsIntegrationDump() and not (set(targets) & non_firmware_targets):
        build += 1
        start, end = section.start() + match.start(2), section.start() + match.end(4)
        config_path.write_text(config[:start] + f"{major}.{feature}.{build}" + config[end:])

    version = f"{major}.{feature}.{build}"
    env["FIRMWARE_VERSION"] = version
    env.Append(CPPDEFINES=[
        ("VERSION", version),
        ("STRVERSION", '\\"' + version + '\\"'),
        ("STRBUILDDATE", '\\"' + date.today().strftime("%Y.%m.%d") + '\\"'),
    ])
    print(f"Firmware version: {version}")


if "Import" in globals():
    Import("env")
    from SCons.Script import COMMAND_LINE_TARGETS
    configure_version(env, COMMAND_LINE_TARGETS)
