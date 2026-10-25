# Zygisk-Il2CppDumper

An experimental Android/Zygisk project for inspecting IL2CPP metadata at runtime. Compatibility depends on the target application's Unity/IL2CPP version and Android environment; successful operation is not guaranteed.

**Authorized use only:** use this project only with applications you own or are explicitly authorized to assess. Do not use it to evade anti-cheat, defeat access controls, or extract information from third-party applications without permission.

中文说明请见 [README.zh-CN.md](README.zh-CN.md)。  
Hướng dẫn tiếng Việt: [README.vi.md](README.vi.md)。

## Repository layout

- `module/src/main/cpp/`: native module and IL2CPP-related source.
- `module/build.gradle`, `build.gradle`, and `gradle/`: Android/Gradle build configuration.
- `.github/workflows/`: automated checks and repository workflows.
- `scripts/configure_target.py`: validates a package identifier before updating build configuration.
- `tests/`: unit tests for configuration validation. These tests do **not** establish runtime compatibility or verify the native module on a device.

## Build environment

Use a supported Android Studio installation with the Android SDK/NDK versions declared by the project. The current project pins Android Gradle Plugin 7.4.2, Gradle 7.5, and NDK 25.2.9519653; update these as a tested set rather than changing one version in isolation.

For a local authorized test build, configure `GamePackageName` in `module/src/main/cpp/game.h` for a package you control, then run the Gradle task `:module:assembleRelease`. The module ZIP is written under `out/`.

The GitHub Actions build workflow accepts a package identifier and validates it before applying configuration. It does not grant permission to inspect the selected application.

## Automated checks

Run the Python validation tests from the repository root:

```sh
python -m unittest discover -s tests -v
python -m compileall -q scripts tests
```

These checks cover package-name validation and configuration behavior only. Native compilation, device testing, and compatibility with specific applications must be assessed separately in an authorized test environment.

## Reporting problems

Please include the Android version, device ABI, build environment, exact build error, and a minimal reproduction. Remove personal data, proprietary application files, metadata dumps, signing keys, and other secrets before posting logs or attachments. See [SECURITY.md](SECURITY.md) for security reports.
