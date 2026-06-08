/**
 * @file config.h
 * @brief 全局配置常量与硬件参数
 *
 * 集中管理所有硬件引脚定义、EEPROM 地址映射、默认参数和全局状态变量。
 * 每个模块通过 #include "config.h" 获取共享配置。
 */

#pragma once
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>

// ──── 硬件引脚 ────────────────────────────────────────────
#define LED_PIN       2          // 板载 LED (GPIO2, 低电平点亮)
#define LED_ON        LOW
#define LED_OFF       HIGH

// ──── 串口波特率 ──────────────────────────────────────────
#define BAUD_BOOT     74880      // ESP8266 启动默认波特率
#define BAUD_RUNTIME  115200     // 应用运行波特率

// ──── EEPROM 布局（共 512 字节） ─────────────────────────
namespace EepromAddr {
    constexpr uint16_t MAGIC       = 0;     // 魔数 (uint16_t)
    constexpr uint16_t SSID        = 4;     // WiFi SSID (32 字节)
    constexpr uint16_t PASS        = 36;    // WiFi 密码 (64 字节)
    constexpr uint16_t MQTT_SERVER = 100;   // MQTT 服务器地址 (39 字节)
    constexpr uint16_t MQTT_PORT   = 140;  // MQTT 端口, 0x8C (uint16_t)
    constexpr uint16_t MQTT_USER   = 142;  // MQTT 用户名/Token (32 字节)
}

#define EEPROM_TOTAL_SIZE   512
#define EEPROM_MAGIC_VALUE  0xE5F1

// ──── 字符串缓冲区大小 ────────────────────────────────────
#define MAX_SSID_LEN   32
#define MAX_PASS_LEN   64
#define MAX_HOST_LEN   40
#define MAX_TOPIC_LEN  40
#define MAX_MQTT_USER_LEN  32

// ──── MQTT 默认参数 ──────────────────────────────────────
#define DEFAULT_MQTT_SERVER   "150.158.19.191"
#define DEFAULT_MQTT_PORT     1883
#define DEFAULT_MQTT_USER     "lmxupyH3ORJVOdoYXEy5"
#define DEFAULT_TOPIC_STATUS  "v1/devices/me/telemetry"
#define DEFAULT_TOPIC_CMD     "v1/devices/me/rpc/request/+"

// ──── DNS 降级备用 IP ────────────────────────────────────
#define MQTT_FALLBACK_IP      "150.158.19.191"

// ──── 超时与间隔 ──────────────────────────────────────────
#define WIFI_CONNECT_TIMEOUT_MS    30000   // WiFi 手动连接超时
#define WIFI_STABLE_CHECK_MS       5000    // 连接稳定性检查时长
#define WIFI_AUTO_CONNECT_TIMEOUT_MS 30000 // 自动连接超时
#define MQTT_RECONNECT_INTERVAL_MS  10000   // MQTT 断线重连间隔
#define MQTT_STATUS_INTERVAL_MS     30000   // 状态心跳发布间隔
#define LED_BLINK_INTERVAL_MS      1000    // LED 闪烁间隔
#define LED_BLINK_ENABLED           0       // 呼吸灯开关 (0=关闭, 1=开启)
#define MENU_TIMEOUT_SEC            5       // 菜单自动选择超时
#define BRIDGE_WAIT_NEXT_CHAR_MS    20      // 桥接协议第二字符等待
#define BRIDGE_MAX_LINE_LEN         256     // 桥接协议单行最大长度
#define BRIDGE_MAX_TOPIC_LEN        128     // 桥接 MQTT topic 最大长度
#define BRIDGE_MAX_PAYLOAD_LEN       512     // 桥接 MQTT payload 最大长度

// ═══════════════════════════════════════════════════════════
// 全局状态（extern 声明，在 main.cpp 中定义）
// ═══════════════════════════════════════════════════════════

// WiFi 状态
extern char     g_wifi_ssid[MAX_SSID_LEN + 1];
extern char     g_wifi_pass[MAX_PASS_LEN + 1];
extern bool     g_wifi_configured;
extern bool     g_wifi_connected;

// MQTT 状态
extern char         g_mqtt_server[MAX_HOST_LEN + 1];
extern uint16_t     g_mqtt_port;
extern char         g_mqtt_user[MAX_MQTT_USER_LEN + 1];
extern char         g_mqtt_topic_sta[MAX_TOPIC_LEN + 1];
extern char         g_mqtt_topic_cmd[MAX_TOPIC_LEN + 1];
extern WiFiClient   g_wifi_client;
extern PubSubClient g_mqtt_client;
extern bool         g_mqtt_connected;
extern unsigned long g_last_status_pub;

// LED 状态
extern unsigned long g_last_blink;
extern bool         g_led_state;

#endif // CONFIG_H
