# Zygisk-Il2CppDumper

这是一个实验性的 Android/Zygisk 项目，用于在运行时检查 IL2CPP 元数据。实际兼容性取决于目标应用的 Unity/IL2CPP 版本和 Android 环境，不保证所有应用都能正常运行。

**仅限获授权的用途：** 只可用于你拥有或明确获得测试授权的应用。不得用于规避反作弊、绕过访问控制，或未经许可提取第三方应用的数据。

英文说明请见 [README.md](README.md)。  
越南语说明请见 [README.vi.md](README.vi.md)。

## 目录结构

- `module/src/main/cpp/`：原生模块及 IL2CPP 相关源代码。
- `module/build.gradle`、`build.gradle` 和 `gradle/`：Android/Gradle 构建配置。
- `.github/workflows/`：自动化检查和仓库工作流。
- `scripts/configure_target.py`：在更新构建配置前验证应用包名。
- `tests/`：构建配置校验的单元测试。这些测试**不能**证明原生模块在设备上能够运行，也不能证明目标应用兼容。

## 构建环境

请使用包含项目所需 Android SDK/NDK 的 Android Studio 环境。当前项目使用 Android Gradle Plugin 7.4.2、Gradle 7.5 和 NDK 25.2.9519653。升级时应将这些版本作为一组进行测试，不要只单独修改其中一个版本。

进行本地授权测试构建时，请在 `module/src/main/cpp/game.h` 中将 `GamePackageName` 设置为你拥有或有权测试的应用包名，然后运行 Gradle 任务 `:module:assembleRelease`。模块 ZIP 文件会生成在 `out/` 目录下。

GitHub Actions 构建工作流会在应用包名写入配置前先进行校验。能够构建模块并不代表你已获得检查目标应用的授权。

## 自动化检查

在仓库根目录运行：

```sh
python -m unittest discover -s tests -v
python -m compileall -q scripts tests
```

这些检查只覆盖包名校验和配置行为。原生编译、设备测试以及对特定应用的兼容性必须在经过授权的测试环境中单独确认。

## 问题反馈

请提供 Android 版本、设备 ABI、构建环境、完整错误信息和最小复现步骤。发布日志或附件前，请删除个人数据、专有应用文件、元数据转储、签名密钥及其他机密信息。安全问题请参阅 [SECURITY.md](SECURITY.md)。
