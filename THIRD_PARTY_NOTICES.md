# 第三方组件

| 组件 | 固定版本 / 来源 | 许可证 |
| --- | --- | --- |
| jsmn | [v1.1.0](https://github.com/zserge/jsmn/tree/v1.1.0) | [MIT](licenses/jsmn-MIT.txt)，Copyright (c) 2010 Serge Zaitsev |
| ArduinoJson | [6.21.5](https://github.com/bblanchon/ArduinoJson/tree/v6.21.5) | [MIT](licenses/ArduinoJson-MIT.txt) |
| NimBLE-Arduino | [1.4.3](https://github.com/h2zero/NimBLE-Arduino/tree/1.4.3) | [Apache 2.0](licenses/NimBLE-Apache-2.0.txt)，组件内部保留其各文件原始许可 |
| Arduino-ESP32 | [2.0.17 源码](https://github.com/espressif/arduino-esp32/tree/2.0.17) | [LGPL 2.1](licenses/Arduino-ESP32-LGPL-2.1.txt)，部分文件另有许可 |
| libb64 | Arduino-ESP32 随附 | [原始许可](licenses/libb64.txt) |
| ESP-IDF | Arduino-ESP32 2.0.17 对应的 4.4 系列 | [官方源码与许可](https://github.com/espressif/esp-idf/tree/v4.4.7) |

本项目未修改上述第三方源码；固件通过 PlatformIO 获取依赖并静态链接。源码版本由 `platformio.ini` 与构建文档记录，可以使用本项目源码和相同依赖重新编译固件。第三方组件内部文件的版权和许可继续适用。

Windows 接收器使用 jsmn，发布附件同时提供 MIT 许可。Zig 是构建工具，EXE 没有随附 Zig 运行时；仅导入 Windows 系统 DLL。Android APK 使用 Android 平台 API，无外部 Maven 运行时库。
