# AI 文字桥源码

当前固件 2.2.0、安卓端 2.0.0、Windows 端 1.0.0。

请从仓库根目录 [README](../README.md) 开始，或阅读 [使用指南](dist/使用说明.md)、[构建与刷机](../docs/BUILD.md)、[协议](../docs/PROTOCOL.md)、[验证范围](../docs/VALIDATION.md)。

构建顺序：安卓 APK → Windows EXE → firmware/tools/embed_web.py → PlatformIO 固件 → tools/package_release.py。构建产物和私钥不进入 Git，二进制由 GitHub Release 提供。
