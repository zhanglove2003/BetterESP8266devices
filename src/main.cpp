/**
 * @file main.cpp
 * @brief ESP8266 固件入口 — WiFi + MQTT + STM32 Bridge v3.0
 *
 * 本文件仅包含：
 *   - 全局状态变量定义
 *   - setup() 初始化流程
 *   - loop() 主循环（心跳、MQTT 维护、串口命令）
 *
 * 业务逻辑已拆分到独立模块：
 *   config.h       - 硬件常量与全局声明
 *   serial_utils.h - 串口输入工具
 *   eeprom_store.h - EEPROM 读写
 *   wifi_manager.h - WiFi 连接管理
 *   mqtt_manager.h - MQTT 通信管理
 *   menu.h         - 交互菜单与运行时命令
 *   bridge.h       - STM32 串口桥接协议
 */

#include <Arduino.h>
#include "config.h"
#include "serial_utils.h"
#include "eeprom_store.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "menu.h"
#include "bridge.h"

// ═══════════════════════════════════════════════════════════
// 全局状态定义（在 config.h 中 extern 声明）
// ═══════════════════════════════════════════════════════════

// WiFi
char     g_wifi_ssid[MAX_SSID_LEN + 1]  = {0};
char     g_wifi_pass[MAX_PASS_LEN + 1]  = {0};
bool     g_wifi_configured = false;
bool     g_wifi_connected  = false;

// MQTT
char         g_mqtt_server[MAX_HOST_LEN + 1]   = DEFAULT_MQTT_SERVER;
uint16_t     g_mqtt_port          = DEFAULT_MQTT_PORT;
char         g_mqtt_user[MAX_MQTT_USER_LEN + 1] = DEFAULT_MQTT_USER;
char         g_mqtt_topic_sta[MAX_TOPIC_LEN + 1] = DEFAULT_TOPIC_STATUS;
char         g_mqtt_topic_cmd[MAX_TOPIC_LEN + 1] = DEFAULT_TOPIC_CMD;
WiFiClient   g_wifi_client;
PubSubClient g_mqtt_client(g_wifi_client);
bool         g_mqtt_connected = false;
unsigned long g_last_status_pub = 0;

// LED
unsigned long g_last_blink = 0;
bool         g_led_state  = false;

// ═══════════════════════════════════════════════════════════
// setup() — 初始化流程
// ═══════════════════════════════════════════════════════════

void setup() {
    // 切换到应用波特率
    Serial.begin(BAUD_BOOT);
    delay(200);
    Serial.println(F("\n--- ESP8266 Boot ---"));
    Serial.flush();
    Serial.end();
    Serial.begin(BAUD_RUNTIME);
    delay(100);

    // 硬件初始化
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);

    // EEPROM 初始化
    eeprom_init();

    // 启动闪烁指示
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, LED_ON);
        delay(80);
        digitalWrite(LED_PIN, LED_OFF);
        delay(80);
    }

    // 打印芯片信息
    print_chip_info();

    // 加载 MQTT EEPROM 配置
    eeprom_load_mqtt();
    // 强制同步到当前编译默认值（修复 EEPROM 残留的旧配置）
    strncpy(g_mqtt_server, DEFAULT_MQTT_SERVER, MAX_HOST_LEN);
    g_mqtt_port = DEFAULT_MQTT_PORT;
    strncpy(g_mqtt_user, DEFAULT_MQTT_USER, MAX_MQTT_USER_LEN);
    eeprom_save_mqtt();

    // 启动菜单（无超时，等待用户主动选择）
    show_menu();
    int choice = wait_menu_choice();
    handle_menu_choice(choice);

    // 尝试 EEPROM 自动连接（仅在用户未手动连接时，且用户未主动跳过过）
    if (!g_wifi_connected && eeprom_load_wifi()) {
        wifi_auto_connect();
    }

    // 连接 MQTT
    if (g_wifi_connected) {
        Serial.println(F("正在连接 MQTT..."));
        mqtt_connect();
    }

    Serial.println(F(">>> 运行中 (m=菜单 p=发布 r=重启)"));
    Serial.println();
}

// ═══════════════════════════════════════════════════════════
// loop() — 主循环（非阻塞）
// ═══════════════════════════════════════════════════════════

void loop() {
    // LED 心跳闪烁（可通过 LED_BLINK_ENABLED 开关）
    #if LED_BLINK_ENABLED
    unsigned long now = millis();
    if (now - g_last_blink >= LED_BLINK_INTERVAL_MS) {
        g_last_blink = now;
        g_led_state = !g_led_state;
        digitalWrite(LED_PIN, g_led_state ? LED_ON : LED_OFF);
    }
    #endif

    // WiFi + MQTT 维护
    if (g_wifi_connected) {
        // MQTT 断线重连
        if (!g_mqtt_client.connected()) {
            static unsigned long last_reconnect = 0;
            if (millis() - last_reconnect > MQTT_RECONNECT_INTERVAL_MS) {
                last_reconnect = millis();
                mqtt_connect();
            }
        }
        g_mqtt_client.loop();

        // 定时发布状态心跳
        if (g_mqtt_connected && millis() - g_last_status_pub > MQTT_STATUS_INTERVAL_MS) {
            g_last_status_pub = millis();
            mqtt_publish_status();
            // 关键修复：publish 失败立即清标志位，下一轮自动重连
            g_mqtt_connected = g_mqtt_client.connected();
        }
    }

    // 串口命令处理
    if (Serial.available()) {
        char cmd = Serial.read();

        // STM32 桥接协议（>> 前缀）
        if (cmd == '>') {
            if (check_bridge_prefix()) return;
        }

        // 本地运行时命令
        handle_runtime_cmd(cmd);
    }
}
