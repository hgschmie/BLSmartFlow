"""Reserve a new major.feature.build version for each firmware build."""
from datetime import date
from pathlib import Path
import os
import re


def update_project_checksum(source, target, env):
    """Keep PlatformIO's next invocation from deleting the build we just made.

    PlatformIO records its project checksum before pre-build scripts run. Our
    version reservation changes platformio.ini, and HTML generation may add
    headers. Record the final state only after the firmware was built successfully.
    """
    from platformio.project.config import ProjectConfig
    from platformio.project.helpers import compute_project_checksum

    config = ProjectConfig(env.subst("$PROJECT_CONFIG"))
    checksum_path = Path(env.subst("$PROJECT_BUILD_DIR")) / "project.checksum"
    checksum_path.write_text(compute_project_checksum(config))


def configure_version(env, targets, release_version=None):
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
        if release_version is not None:
            if not re.fullmatch(r"\d+\.\d+\.\d+", release_version):
                raise ValueError("BLSF_RELEASE_VERSION must use major.feature.build format")
            major, feature, build = map(int, release_version.split('.'))
        else:
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
    configure_version(env, COMMAND_LINE_TARGETS, os.environ.get("BLSF_RELEASE_VERSION"))
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", update_project_checksum)
