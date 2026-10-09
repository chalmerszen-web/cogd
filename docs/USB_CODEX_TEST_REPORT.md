# ESP32-C3 USB 到当前 Codex 聊天测试

日期：2026-10-10（Asia/Shanghai）。范围：对已连接的 ESP32-C3 做一次 USB 消息回传、转入当前聊天及触发提问的实机测试。

结论：**一次经电脑转发的链路验证通过，用户已确认看到问题。** 本轮使用 Codex 的串口工具和聊天转发工具完成中转；没有部署脱离当前助手运行的常驻桥接程序，也没有验证单靠插入 USB 就能自动向聊天发送消息。

## 实际路径

电脑发送固定测试指令 → ESP32-C3 现有 Agent 返回文字 → 原生 USB Serial/JTAG（COM5）→ 电脑读取 → Codex 聊天转发工具 → 当前聊天 → Codex 提问 → 用户确认。

- Windows 枚举：COM5，VID 303A / PID 1001。
- 设备实测：ESP-HI head，ESP32-C3 rev0.4，4 MiB Flash，运行版本 0.12.0-rc2。
- 当前聊天：01a121f1-cd9b-77f0-ad1b-39859a4d6b2b。
- 唯一测试编号：USB_CODEX_PROBE_20261010_A7F3。
- 使用现有工具 tools/usb_command.py 与 tools/serial_link.py；115200，DTR/RTS 均为 false。

## 收到的真实 USB 内容

```text
USB_CODEX_PROBE_20261010_A7F3 37+58=95。是否收到来自ESP32-C3的USB测试？
@done
```

原始命令要求设备逐字输出一个计算及提问请求；设备实际先算出 95，再输出问题。因此逐字回显不通过，但编号、中文内容和提问都通过真实 USB 返回。转发时保留了实际收到的这一行，没有把预期命令冒充设备输出。

当前固件的 agent chat 会使用已有联网模型配置生成回答。因此本次是主机发起的设备回传测试，不能据此认定板载离线程序已经能自主发起 Codex 请求。

## 验证

| 检查 | 结果 | 证据 |
|---|---|---|
| 原生 USB 枚举及设备查询 | PASS | 01-device-probe.json：status、hardware、wake status |
| 带编号的中文消息从设备回传 | PASS | 02-device-message.json，收到 @done |
| 精确逐字回显 | FAIL | 设备先完成了算术；未逐字保留原请求 |
| 转入当前 Codex 聊天 | PASS | 转发工具 isError=false，当前会话收到同编号消息 |
| Codex 根据测试消息发起提问 | PASS | request_user_input_async 接受问题 |
| 用户可见确认 | PASS | 用户回复“已看到这条测试问题” |
| 自动持续监听 / 空闲时设备自主唤起聊天 | NOT RUN | 未安装常驻桥接程序 |
| Codex 回复回写设备 / 设备播报 | NOT RUN | 不属于本次已验证链路 |

第一次查询还读到了设备已有语音诊断及 timeout/protocol 行，原始记录保留；设备查询本身收到匹配的版本和硬件 JSON。这些诊断没有被算作 USB 测试成功或语音质量通过。

## 收尾

末次查询仍为 0.12.0-rc2、busy=false、Wi-Fi 在线、唤醒 enabled=true/listening；串口脚本正常退出并关闭 COM5。没有重新烧录、复位、修改连接配置或安装后台服务。正常 agent chat 会写入设备既有对话历史，因此不声称 Flash 逐字节未变。

证据目录：artifacts/usb-codex-20261010/。01 至 03 为原始串口命令和响应，04 为聊天转发及用户确认记录，05 为汇总校验。

若要持续使用，需要电脑端常驻程序读取 USB 并提交到指定聊天。官方 [Codex App Server 文档](https://learn.chatgpt.com/docs/app-server)提供 thread/resume、turn/start 和 turn/steer 等协议；本轮没有连接或验证独立桥接程序对桌面当前会话的持续控制。
