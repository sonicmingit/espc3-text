# 构建与刷机

## 环境

以下命令从仓库根目录执行。当前构建脚本针对 Windows，建议使用 Python 3.10+ 和 JDK 17。

- 安卓 SDK：Android 35 平台、build-tools 35.0.0，从 [Android 官方工具](https://developer.android.com/tools)安装。
- PC：官方 [Zig 0.13.0](https://ziglang.org/download/0.13.0/) Windows x86_64 编译器，目标为 x86 Windows。
- 固件：PlatformIO 6.x，平台 `espressif32@6.10.0`，Arduino ESP32 2.0.17。
- 烧录：`python -m pip install esptool==4.8.1`。板子须能被系统识别为 USB 串口。

## 1. 构建安卓 APK

```powershell
$env:JAVA_HOME = 'C:\path\to\jdk-17'
$env:ANDROID_SDK_ROOT = 'C:\path\to\Android\Sdk'
python ai-bridge/android/build.py
```

标准 SDK 目录是 `build-tools/35.0.0/` 与 `platforms/android-35/android.jar`；脚本同时兼容官方 ZIP 解压得到的 `android-15/` 与 `android-35/` 布局。可用 `ANDROID_BUILD_TOOLS` 指定 build-tools 的完整目录。

输出为 `ai-bridge/dist/SonicTextBridge-v2.0.0.apk`。脚本使用 aapt2、javac、D8、zipalign、apksigner，不依赖 Gradle 或 Maven。

默认在 `android/signing/` 生成本地测试密钥。该目录被 Git 忽略。公开仓库不包含已发布 APK 的私钥；用自行生成的密钥签名时，通常无法直接覆盖已发布 APK，卸载会清除手机历史。需要持续升级时自行保管同一把签名密钥。

可指定 `SONIC_KEYSTORE`、`SONIC_KEY_ALIAS`、`SONIC_STORE_PASSWORD`、`SONIC_KEY_PASSWORD`。密码经环境变量交给工具，不写入源码或命令行。默认测试口令为 `android`，不用于生产签名。

## 2. 构建 Windows EXE

```powershell
$env:ZIG_EXE = 'C:\path\to\zig-windows-x86_64-0.13.0\zig.exe'
python ai-bridge/receiver-win32/build.py
```

输出为 `ai-bridge/dist/SonicTextReceiver-x86.exe`。构建脚本会执行 `--selftest` 检查 JSON、UTF-8 中文、表情、换行及非法输入。

EXE 使用 Windows 系统 DLL，无 CRT、.NET、Python、浏览器依赖；PE 头的系统/子系统版本设为 5.1，目标 CPU 为 i386。XP、Win 7 的运行与 USB 驱动兼容性需要实际电脑确认。

## 3. 嵌入资源、编译固件

先完成前两步，或把 Release 的 APK 和 EXE 放入 `ai-bridge/dist/`。Release 中带版本号的 EXE 要重命名为 `SonicTextReceiver-x86.exe`。

```powershell
python -m pip install platformio
python ai-bridge/firmware/tools/embed_web.py
python -m platformio run --project-dir ai-bridge/firmware
```

嵌入脚本将 APK、EXE 和两个网页生成到固件头文件。生成文件被 Git 忽略，更新任何一个资源后需要重新执行嵌入和固件编译。

平台配置：`esp32-c3-devkitm-1`、4 MiB Flash、DIO、40 MHz、`huge_app.csv`。平台默认缓存于仓库 `.tools/pio-core/`；可通过 `PLATFORMIO_CORE_DIR` 指定已有缓存。

## 4. 生成发布附件

```powershell
python tools/package_release.py
```

输出到被忽略的 `release-assets/`：固件 ZIP、应用 BIN、APK、带版本号 EXE、PC ZIP、指南和校验和。脚本会确认固件嵌入资源与将发布的资源逐字节一致。

## 5. 刷入固件

下载并解压固件 ZIP，从其目录运行：

```powershell
python -m pip install esptool==4.8.1
python flash.py --port COM5
```

COM5 仅是示例，必须在设备管理器确认对应板子的实际端口。先关闭浏览器串口页面、EXE 接收器和串口监视器。若板子不能自动进入下载模式，按住 BOOT、按一下 RESET，再释放 BOOT 后重试。

脚本写入：

| 文件 | 地址 |
| --- | --- |
| bootloader.bin | 0x0 |
| partitions.bin | 0x8000 |
| boot_app0.bin | 0xE000 |
| firmware.bin | 0x10000 |

分段写入不主动擦除整片 Flash；在本项目同一分区布局下升级，会保留 0x9000 的 NVS 与 0x310000 起的 LittleFS。不要使用 `erase_flash` 或刷入包含空白 NVS 区段的全片镜像来代替升级。

已有其他固件或分区布局时，保留配置的保证不适用。首次 LittleFS 初始化会格式化本项目的文件系统分区，后续挂载失败不自动格式化。

独立 `sonic-ai-bridge-v2.2.0-app.bin` **只用于 0x10000**，不能作为从 0x0 开始的整合镜像烧录。完整首次刷入应使用固件 ZIP。
