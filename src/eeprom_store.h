/**
 * @file eeprom_store.h
 * @brief EEPROM 持久化存储操作
 *
 * 管理 WiFi 凭据和 MQTT 配置的 EEPROM 读写。
 * 使用魔数 0xE5F1 标识有效数据，避免首次上电误读。
 */

#pragma once
#ifndef EEPROM_STORE_H
#define EEPROM_STORE_H

#include "config.h"

/**
 * 初始化 EEPROM 子系统
 * 必须在所有其他 EEPROM 操作之前调用
 */
inline void eeprom_init() {
    EEPROM.begin(EEPROM_TOTAL_SIZE);
}

/**
 * 将当前 WiFi SSID 和密码写入 EEPROM 并提交
 *
 * 前置条件：g_wifi_ssid 和 g_wifi_pass 已填充有效值
 */
inline void eeprom_save_wifi() {
    uint16_t magic = EEPROM_MAGIC_VALUE;
    EEPROM.put(EepromAddr::MAGIC, magic);
    for (size_t i = 0; i < MAX_SSID_LEN; i++)
        EEPROM.write(EepromAddr::SSID + i, g_wifi_ssid[i]);
    for (size_t i = 0; i < MAX_PASS_LEN; i++)
        EEPROM.write(EepromAddr::PASS + i, g_wifi_pass[i]);
    EEPROM.commit();
}

/**
 * 从 EEPROM 加载 WiFi 凭据
 *
 * @return true  魔数匹配且 SSID 有效
 * @return false 无有效数据或 SSID 为空/全 0xFF
 */
inline bool eeprom_load_wifi() {
    uint16_t magic = 0;
    EEPROM.get(EepromAddr::MAGIC, magic);
    if (magic != EEPROM_MAGIC_VALUE) return false;

    for (size_t i = 0; i < MAX_SSID_LEN; i++)
        g_wifi_ssid[i] = EEPROM.read(EepromAddr::SSID + i);
    for (size_t i = 0; i < MAX_PASS_LEN; i++)
        g_wifi_pass[i] = EEPROM.read(EepromAddr::PASS + i);

    g_wifi_ssid[MAX_SSID_LEN] = '\0';
    g_wifi_pass[MAX_PASS_LEN] = '\0';

    // 校验 SSID 首字节是否有效
    if (g_wifi_ssid[0] == '\0' || g_wifi_ssid[0] == 0xFF) return false;
    return true;
}

/**
 * 清除 EEPROM 中的 WiFi 凭据（擦除魔数，清空全局变量）
 *
 * 通常在"完全复位"操作中调用
 */
inline void eeprom_clear_wifi() {
    uint16_t zero = 0;
    EEPROM.put(EepromAddr::MAGIC, zero);
    EEPROM.commit();
    memset(g_wifi_ssid, 0, sizeof(g_wifi_ssid));
    memset(g_wifi_pass, 0, sizeof(g_wifi_pass));
    g_wifi_configured = false;
}

/**
 * 从 EEPROM 加载 MQTT 配置（服务器地址和端口）
 *
 * 若 EEPROM 中数据无效，使用默认值
 */
inline void eeprom_load_mqtt() {
    // 加载端口
    EEPROM.get(EepromAddr::MQTT_PORT, g_mqtt_port);
    if (g_mqtt_port == 0 || g_mqtt_port == 0xFFFF) {
        g_mqtt_port = DEFAULT_MQTT_PORT;
    }

    // 加载服务器地址，过滤非法字符
    for (int i = 0; i < MAX_HOST_LEN; i++) {
        char c = EEPROM.read(EepromAddr::MQTT_SERVER + i);
        g_mqtt_server[i] = (c >= 32 && c < 127) ? c : '\0';
    }
    g_mqtt_server[MAX_HOST_LEN] = '\0';

    // 校验：首字节无效则恢复默认
    if (g_mqtt_server[0] == '\0' || g_mqtt_server[0] == 0xFF) {
        strncpy(g_mqtt_server, DEFAULT_MQTT_SERVER, MAX_HOST_LEN);
    }
}

/**
 * 将 MQTT 配置保存到 EEPROM
 *
 * 前置条件：g_mqtt_server 和 g_mqtt_port 已更新
 */
inline void eeprom_save_mqtt() {
    for (int i = 0; i < MAX_HOST_LEN; i++)
        EEPROM.write(EepromAddr::MQTT_SERVER + i, g_mqtt_server[i]);
    EEPROM.put(EepromAddr::MQTT_PORT, g_mqtt_port);
    EEPROM.commit();
}

#endif // EEPROM_STORE_H
