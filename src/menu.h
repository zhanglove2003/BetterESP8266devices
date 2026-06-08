/**
 * @file menu.h
 * @brief 启动交互菜单与运行时命令
 *
 * 提供启动时的 4 选项菜单（复位/WiFi/MQTT/跳过）和运行时快捷键（m=菜单 r=重启）。
 * 支持 5 秒超时自动选择"跳过"。
 */

#pragma once
#ifndef MENU_H
#define MENU_H

#include "config.h"
#include "serial_utils.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "eeprom_store.h"

/**
 * 显示启动菜单选项
 */
inline void show_menu() {
    serial_flush_input();
    Serial.println();
    Serial.println(F("  =================================="));
    Serial.println(F("         ESP8266 启动菜单"));
    Serial.println(F("  =================================="));
    Serial.println();
    Serial.println(F("    [1] 完全复位"));
    Serial.println(F("    [2] 连接 WiFi"));
    Serial.println(F("    [3] MQTT 配置"));
    Serial.println(F("    [4] 跳过"));
    Serial.println(F("  ----------------------------------"));
    Serial.print(F("\n  请选择 [1/2/3/4] > "));
}

/**
 * 等待用户菜单选择（无限等待，无超时）
 *
 * @return int 用户选择 (1~4)
 */
inline int wait_menu_choice() {
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c >= '1' && c <= '4') {
                Serial.println(c);
                serial_flush_input();
                return c - '0';
            }
        }
        delay(10);
        yield();
    }
}

/**
 * 执行完全复位（需用户输入 "yes" 确认）
 *
 * 操作：清除 EEPROM → LED 闪烁 → 重启
 */
inline void full_reset() {
    Serial.println(F("\n  ============ 完全复位 ============"));
    Serial.println(F("  输入 'yes' 确认 > "));
    serial_flush_input();

    char c[8];
    if (!serial_read_line(c, sizeof(c), 15000)) return;
    if (strcmp(c, "yes") != 0) {
        Serial.println(F("[取消]\n"));
        return;
    }

    eeprom_clear_wifi();
    for (int i = 0; i < 5; i++) {
        digitalWrite(LED_PIN, LED_ON);
        delay(100);
        digitalWrite(LED_PIN, LED_OFF);
        delay(100);
    }
    ESP.restart();
}

/**
 * 处理启动菜单选择结果
 *
 * 子功能返回后自动重新显示菜单，直到用户选择 [4] 跳过。
 * 选择 [4] 时检测 WiFi 状态，未连接则提示用户设置。
 */
inline void handle_menu_choice(int choice) {
    while (true) {
        switch (choice) {
            case 1: full_reset(); return;
            case 2: wifi_scan_and_connect(); break;
            case 3: mqtt_config_menu(); break;
            case 4: {
                // WiFi 未连接时提示用户设置
                if (!g_wifi_connected) {
                    Serial.println(F("\n  [提示] WiFi 未连接"));
                    Serial.println(F("  是否现在设置 WiFi？(y/n) > "));
                    serial_flush_input();
                    char ans[4] = {0};
                    serial_read_line(ans, sizeof(ans), 15000);
                    if (ans[0] == 'y' || ans[0] == 'Y') {
                        wifi_scan_and_connect();
                    } else {
                        Serial.println(F("  已跳过\n"));
                    }
                }
                return;
            }
            default: return;
        }
        // 子功能返回后重新显示菜单
        show_menu();
        choice = wait_menu_choice();
    }
}

/**
 * 处理运行时快捷键
 *
 * 'm'/'M' → 显示菜单并处理选择（含循环菜单）
 * 'r'     → 立即重启
 */
inline void handle_runtime_cmd(char cmd) {
    if (cmd == 'm' || cmd == 'M') {
        serial_flush_input();
        show_menu();
        int c = wait_menu_choice();
        handle_menu_choice(c);
        Serial.println(F(">>> 运行中 (m=菜单 p=发布 r=重启)\n"));
    } else if (cmd == 'p') {
        // 手动 MQTT 发布: p topic payload
        String line = Serial.readStringUntil('\n');
        line.trim();
        int sp = line.indexOf(' ');
        if (sp < 1) {
            Serial.println(F("[用法] p <主题> <消息>"));
        } else if (!g_mqtt_connected || !g_mqtt_client.connected()) {
            Serial.println(F("[失败] MQTT 未连接"));
            g_mqtt_connected = false;
        } else {
            String topic = line.substring(0, sp);
            String payload = line.substring(sp + 1);
            g_mqtt_client.publish(topic.c_str(), payload.c_str());
            // 关键修复：publish 失败立即清标志位
            g_mqtt_connected = g_mqtt_client.connected();
            if (g_mqtt_connected) {
                Serial.printf("[已发布] %s\n", topic.c_str());
            } else {
                Serial.println(F("[失败] publish 失败，将自动重连"));
            }
        }
    } else if (cmd == 'r') {
        Serial.println(F("[重启]"));
        ESP.restart();
    }
}

#endif // MENU_H
