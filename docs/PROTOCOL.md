# 传输协议与数据保存

## BLE

设备名称 `Sonic-AI`。服务 UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`，写入特征 `6e400002-b5a3-f393-e0a9-e50e24dcca9e`，通知特征 `6e400003-b5a3-f393-e0a9-e50e24dcca9e`。

写入需要加密连接，使用 Just Works bonding。每条消息为 UTF-8 JSON 加换行，客户端按 MTU 分包，板子按字节重组。发送、配置操作用 `pin` 字段提供设备管理密码。

```json
{"op":"send","pin":"SonicC3-2026","mode":"raw","requestId":"example-1","text":"中文输入"}
```

`mode` 支持 `raw`、`polish`、`summary`、`todo`、`translate`、`custom`。`op:status` 查询状态，`op:config` 的配置放在 `config` 对象。通知包括 `status`、`accepted`、`job`、`result`、`saved`、`error`。

## USB 串口

115200 / 8N1，UTF-8，命令和 JSON 响应以换行分隔。

| 命令 | 行为 |
| --- | --- |
| `HELLO`、`STATUS` | 返回 `type:status` |
| `POLL` | 获取队首；空队列时不返回文字帧 |
| `ACK 17` | 仅队首 ID 匹配时出队 |
| `RELEASE` | 释放 USB 接收所有权 |
| `RXTEST` | 空闲且队列为空时生成一条合成测试文字，不调用模型 |

结果示例：

```json
{"type":"text","boot":"example-boot","id":17,"text":"收到的文字"}
```

接收端在成功存入本窗口记录并显示后 ACK。ACK 前重复 POLL 返回同一条；按 `boot:id` 去重。ACK 表示当前接收端已接收，不代表电脑已经保存到磁盘。

## HTTP

配置页 `/`、安卓安装包 `/app.apk`、PC EXE `/receiver.exe`、局域网接收页 `/receive`。状态 `/api/status` 不含 Token 或 Wi-Fi 密码。

以下 API 需要 `X-Device-Key` 请求头，值为设备管理密码：

| 路径 | 方法 | 用途 |
| --- | --- | --- |
| `/api/config` | GET / POST | 读取非敏感配置、保存配置；不会回显 Wi-Fi 密码和 Token |
| `/api/wifi/scan` | POST / GET | 启动异步扫描、读取结果 |
| `/api/model-test` | POST / GET | 启动简短模型调用、查询结果 |
| `/api/last-call` | GET | 最新请求体、响应、错误 |
| `/api/receive?client=web-example` | GET | 读取队首文字或 `type:idle` |
| `/api/receive/ack` | POST | `{ "client":"web-example", "boot":"...", "id":17 }` |
| `/api/receive/release` | POST | `{ "client":"web-example" }` |
| `/api/receive/test` | POST | `{ "client":"web-example" }`，生成本地测试文字 |

`client` 为 1–64 个 ASCII 字母、数字、`-` 或 `_`。USB 与局域网共用 4 条结果队列，通过 8 秒租约避免不同接收端同时取走内容。其他活跃接收端会得到 HTTP 409 或 USB 错误；停止后释放，异常关闭时等待租约过期。

模型 API 使用 `POST /chat/completions`、Bearer Token、`stream:false`，解析 `choices[0].message.content`。HTTPS 校验证书并通过 NTP 校时。长耗时网络调用在独立 worker 中运行。

## 保存边界

- 手机：草稿和最近 100 条历史，App 私有数据；卸载/清除数据会丢失。
- 板子 NVS：Wi-Fi、接口地址、Token、模型、管理密码、处理指令等。
- 板子 LittleFS：仅最新一条模型调用，请求开始和结束更新；连通性测试也会覆盖，原文转发不会覆盖。不记录 Authorization 头和 URL 查询部分，但输入文字、模型输出可能包含用户信息。
- 板子 RAM：最多 4 条电脑待收结果，断电清除。
- PC：每个接收窗口最近 200 条；EXE 可手动保存 UTF-8 BOM 文本，页面刷新或窗口关闭会清除窗口记录。

限制：输入 4096 UTF-8 字节、输出 8192 字节、HTTP 响应 32768 字节；单次一个模型任务。

串口中还保留开发诊断命令 `SELFTEST`、`SELFTEST PROBE`、`SELFTEST FAIL`、`PROBE`、`SCAN`、`SCANRESULT`、`TRACESELF`、`CLEARSELF`、`CMD JSON`。仅在测试板上使用；`CMD` 可以修改配置，历史 BLE 测试也会修改配置，不应在含有用户配置的板子上直接运行。
