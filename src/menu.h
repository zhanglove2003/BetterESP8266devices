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
    Serial.printf("  %d 秒后自动选择 [4]\n", MENU_TIMEOUT_SEC);
    Serial.print(F("\n  请选择 [1/2/3/4] > "));
}

/**
 * 等待用户菜单选择
 *
 * @param timeout_sec 超时秒数
 * @return int 用户选择 (1~4)，超时返回 4
 */
inline int wait_menu_choice(uint32_t timeout_sec) {
    uint32_t deadline = millis() + timeout_sec * 1000UL;
    while (millis() < deadline) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c >= '1' && c <= '4') {
                Serial.println(c);
                serial_flush_input();
                return c - '0';
            }
        }
        delay(10);
    }
    Serial.println(F("\n[超时] 自动选择 [4]"));
    return 4;
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
 * @param choice 菜单选项 (1~4)
 */
inline void handle_menu_choice(int choice) {
    switch (choice) {
        case 1: full_reset(); return;
        case 2: wifi_scan_and_connect(); break;
        case 3: mqtt_config_menu(); break;
        case 4:
        default: break;
    }
}

/**
 * 处理运行时快捷键
 *
 * 'm'/'M' → 显示菜单并处理选择
 * 'r'     → 立即重启
 */
inline void handle_runtime_cmd(char cmd) {
    if (cmd == 'm' || cmd == 'M') {
        serial_flush_input();
        show_menu();
        int c = wait_menu_choice(MENU_TIMEOUT_SEC);
        if (c == 1) {
            full_reset();
        } else if (c == 2) {
            wifi_scan_and_connect();
        } else if (c == 3) {
            mqtt_config_menu();
        }
        Serial.println(F(">>> 运行中 (m=菜单 r=重启)\n"));
    } else if (cmd == 'r') {
        Serial.println(F("[重启]"));
        ESP.restart();
    }
}

#endif // MENU_H
