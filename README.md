# BetterESP8266devices

ESP8266 固件项目，实现 WiFi 连接、MQTT 通信与 STM32 串口桥接功能。

基于 PlatformIO + Arduino 框架开发，目标板型 ESP-12E (ESP8266EX)。

## 功能特性

- **WiFi 管理** — 扫描周围网络、交互式连接、EEPROM 持久化凭据、自动重连
- **MQTT 通信** — 发布设备状态（30s 心跳 JSON）、订阅远程命令（reboot/led_on/led_off）、支持运行时修改 broker 配置
- **STM32 串口桥接** — 通过 `>>` 前缀命令协议，允许 STM32 经由 ESP8266 发布/订阅 MQTT 消息、查询设备信息
- **中文交互菜单** — 启动时 4 选项菜单，5 秒超时自动跳过；运行时支持 `m`（菜单）和 `r`（重启）快捷键
- **稳定性优化** — `WIFI_NONE_SLEEP` 防断连、DNS 解析失败自动切换预置 IP、非阻塞 loop() 设计

## 硬件要求

| 项目 | 规格 |
|------|------|
| 芯片 | ESP8266EX |
| Flash | 4MB (EON), DIO 模式 |
| 晶振 | 26MHz |
| USB 桥接 | CH340 |
| LED 引脚 | GPIO2 (低电平点亮) |

## 引脚定义

| 引脚 | 功能 | 备注 |
|------|------|------|
| GPIO2 | LED | 低电平点亮 (LED_ON=LOW) |
| TX/RX | 串口 | 115200bps (应用) / 74880bps (boot) |

## STM32 桥接协议

ESP8266 与 STM32 通过串口通信，使用以下命令格式：

| 命令 | 格式 | 说明 |
|------|------|------|
| 发布消息 | `>>PUB <topic> <payload>` | 通过 MQTT 发布消息 |
| 订阅主题 | `>>SUB <topic>` | 订阅指定 MQTT 主题 |
| 设备信息 | `>>INFO` | 返回 IP/RSSI/HEAP/UPTIME |
| 帮助 | `>>HELP` | 显示可用命令 |

响应以 `<<` 前缀返回，例如 `<<OK PUB esp8266/test` 或 `<<ERR`。

### INFO 响应示例

```
<<INFO IP=192.168.1.100 RSSI=-45 HEAP=32800 UPTIME=1200
```

## EEPROM 存储布局

| 地址 | 大小 | 内容 |
|------|------|------|
| 0x000 | 2B | Magic (0xE5F1) |
| 0x004 | 32B | WiFi SSID |
| 0x024 | 64B | WiFi 密码 |
| 0x064 | 40B | MQTT Broker 地址 |
| 0x08C | 2B | MQTT 端口 |

总大小 512 字节。

## 快速开始

### 环境准备

1. 安装 [PlatformIO](https://platformio.org/)
2. 克隆仓库
   ```bash
   git clone https://github.com/zhanglove2003/BetterESP8266devices.git
   cd BetterESP8266devices
   ```

### 构建与烧录

```bash
# 编译
pio run

# 烧录 (修改 platformio.ini 中的 upload_port 为你的串口)
pio run -t upload

# 串口监控
pio device monitor -b 115200
```

### 首次使用

1. 烧录后打开串口监视器 (115200bps)
2. 出现启动菜单后，输入 `2` 进入 WiFi 连接
3. 选择网络并输入密码，连接成功后自动保存
4. MQTT 默认连接 `broker.emqx.io:1883`，可通过菜单 `3` 修改

### MQTT 主题

| 主题 | 方向 | 内容 |
|------|------|------|
| `esp8266/status` | 发布 | JSON: `{"uptime":120,"rssi":-45,"heap":32800}` |
| `esp8266/cmd` | 订阅 | 接收命令: `reboot`, `led_on`, `led_off` |

## 项目结构

```
BetterESP8266devices/
├── src/
│   ├── config.h          # 硬件常量、EEPROM 布局、全局状态声明
│   ├── serial_utils.h    # 串口输入工具（行读取、缓冲区清除）
│   ├── eeprom_store.h    # EEPROM 持久化读写（WiFi + MQTT 配置）
│   ├── wifi_manager.h    # WiFi 扫描/连接/自动重连/芯片信息
│   ├── mqtt_manager.h    # MQTT 连接/发布/订阅/DNS 降级
│   ├── menu.h            # 启动菜单/运行时快捷键/完全复位
│   ├── bridge.h          # STM32 串口桥接协议 (>>PUB/SUB/INFO)
│   └── main.cpp          # 入口：setup() + loop() + 全局变量定义
├── platformio.ini         # PlatformIO 构建配置
├── .gitignore             # Git 忽略规则
├── README.md              # 本文档
└── LICENSE                # MIT 许可证
```

## 依赖

| 库 | 版本 | 用途 |
|----|------|------|
| [PubSubClient](https://github.com/knolleary/pubsubclient) | 2.8 | MQTT 客户端 |

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE) 文件。
