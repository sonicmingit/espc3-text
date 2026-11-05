# Sonic AI 文字桥 v2.2.0

首次 GitHub 发布，包含 ESP32-C3 固件、安卓手机端和 Windows / 浏览器接收端。

| 组件 | 版本 | 下载文件 |
| --- | --- | --- |
| ESP32-C3 固件 | 2.2.0 | `sonic-ai-bridge-firmware-v2.2.0.zip` |
| 安卓手机端 | 2.0.0 | `SonicTextBridge-v2.0.0.apk` |
| PC 免安装接收器 | 1.0.0 | `SonicTextReceiver-v1.0.0-x86.exe` 或 Windows ZIP |
| Chrome / Edge USB 页面 | 随套件 | `chrome-usb-receiver.html`，也包含在 Windows ZIP |
| 局域网接收页面 | 随固件 | 刷入后打开 `http://板子局域网IP/receive` |

手机通过 BLE 发送文字；板子可以原文转发，也可以连接 Wi-Fi 调用可配置的 Chat Completions 兼容接口，支持润色、摘要、待办、翻译和自定义指令。配置页支持 Wi-Fi 搜索、模型连通性测试以及最新调用记录。

## 安装与连接

1. 首次刷入或升级：下载并解压固件 ZIP，安装 `esptool==4.8.1`，按 `FLASHING.md` 执行 `python flash.py --port 实际串口`。脚本在当前项目分区布局下保留 NVS 和 LittleFS。
2. 手机安装 APK，在“设备”页搜索并连接 **Sonic-AI**。初始设备管理密码为 `SonicC3-2026`。
3. 电脑用 USB EXE 连接实际串口，或通过局域网浏览器访问 `/receive`。电脑可以连接路由器网线。
4. 手机先选择“原文发送”测试。原文 + USB 不需要 Wi-Fi；远程模型处理需要板子联网。

首次配置可连接无需密码的 **Sonic-AI** 热点并打开 `http://192.168.4.1/`。板子可通过 `/app.apk` 和 `/receiver.exe` 提供对应 APK 与 EXE。

独立 `sonic-ai-bridge-v2.2.0-app.bin` 是应用镜像，**只写入 0x10000**，不是 0x0 整合镜像。完整刷机请使用固件 ZIP。固件固定目标为 ESP32-C3 / 4 MiB / DIO / 40 MHz。

## 验证状态

这是当前开发成果的可下载版本。固件编译、APK 构建与 v2/v3 签名、EXE 解析自检、PE 依赖检查、页面夹具验证、嵌入资源一致性与附件 SHA-256 检查已通过。

2.2 固件尚未刷入实物板，新增 EXE / 局域网链路仍待完整实测。此前 2.0 / 2.1 在板子上完成 BLE → USB、Wi-Fi 扫描及模拟模型测试。XP / Win 7、旧浏览器和目标安卓手机仍需真机测试；EXE 的 XP 5.1 编译目标不代表 USB 驱动已兼容。当前版本不包含录音转写和模拟键盘输入。

所有截图均来自本地 UI 夹具，使用合成输入和模拟返回，没有真实服务 Token。详情见仓库的使用指南、构建文档和 `docs/VALIDATION.md`。

附件包含 `USER-GUIDE-zh-CN.md`、`release-manifest.json`、`SHA256SUMS.txt` 和第三方许可。建议使用 Windows ZIP，以一并保留指南和许可文本。
