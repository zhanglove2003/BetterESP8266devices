/**
 * @file mqtt_manager.h
 * @brief MQTT 通信管理
 *
 * 封装 MQTT 连接、消息回调、状态发布和配置菜单。
 * 关键修复：DNS 解析失败自动降级到预置 IP。
 */

#pragma once
#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "config.h"
#include "serial_utils.h"
#include "eeprom_store.h"

/**
 * MQTT 消息回调（内部使用）
 *
 * 支持的命令：reboot（重启）、led_on（开灯）、led_off（关灯）
 */
inline void mqtt_message_callback(char *topic, byte *payload, unsigned int length) {
    // 安全拷贝：限制到缓冲区大小减 1（预留 '\0'）
    constexpr size_t BUF_SIZE = 128;
    char buf[BUF_SIZE];
    unsigned int len = (length < BUF_SIZE - 1) ? length : (BUF_SIZE - 1);
    memcpy(buf, payload, len);
    buf[len] = '\0';
    Serial.printf("<<MQTT %s %s\n", topic, buf);

    if (strcmp(buf, "reboot") == 0) {
        delay(100);
        ESP.restart();
    } else if (strcmp(buf, "led_on") == 0) {
        digitalWrite(LED_PIN, LED_ON);
    } else if (strcmp(buf, "led_off") == 0) {
        digitalWrite(LED_PIN, LED_OFF);
    }
}

/**
 * 建立 MQTT 连接
 *
 * 流程：DNS 解析 → 降级到预置 IP（如 DNS 失败） → 设置回调 → 连接 → 订阅命令主题
 *
 * @return true  连接成功
 * @return false WiFi 未连接或 broker 不可达
 */
inline bool mqtt_connect() {
    if (!g_wifi_connected || WiFi.status() != WL_CONNECTED) return false;

    IPAddress ip;
    // 关键修复：DNS 失败自动切换到预置 IP
    if (!WiFi.hostByName(g_mqtt_server, ip)) {
        ip.fromString(MQTT_FALLBACK_IP);
    }
    delay(500);

    g_mqtt_client.setServer(ip, g_mqtt_port);
    g_mqtt_client.setCallback(mqtt_message_callback);

    // 使用 MAC 地址后 3 字节生成 Client ID
    char cid[32];
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(cid, sizeof(cid), "ESP8266_%02X%02X%02X", mac[3], mac[4], mac[5]);

    // ThingsBoard 认证：Access Token 作为 username，password 留空
    // 官方文档：https://thingsboard.io/docs/reference/mqtt-api/
    // PubSubClient.connect(clientId, username=token, password=null)
    const char *mqtt_username = g_mqtt_user[0] ? g_mqtt_user : nullptr;
    if (g_mqtt_client.connect(cid, mqtt_username, nullptr)) {
        g_mqtt_client.subscribe(g_mqtt_topic_cmd);
        g_mqtt_connected = true;
        if (g_mqtt_user[0]) {
            Serial.printf("[MQTT] 已连接 %s:%d (Token: %s)\n", g_mqtt_server, g_mqtt_port, g_mqtt_user);
        } else {
            Serial.printf("[MQTT] 已连接 %s:%d\n", g_mqtt_server, g_mqtt_port);
        }
        // 连接后立即发布一条测试遥测，验证端到端连通
        char test_pl[64];
        snprintf(test_pl, sizeof(test_pl), "{\"uptime\":0,\"rssi\":%d,\"heap\":%u}",
                 WiFi.RSSI(), ESP.getFreeHeap());
        if (g_mqtt_client.publish(g_mqtt_topic_sta, test_pl)) {
            Serial.printf("[MQTT] 测试遥测已发送 → %s\n", test_pl);
        } else {
            Serial.println(F("[MQTT] 测试遥测发送失败！"));
        }
        g_mqtt_connected = g_mqtt_client.connected();
        return true;
    }

    Serial.printf("[MQTT] 连接失败 %s:%d 状态码:%d\n", g_mqtt_server, g_mqtt_port, g_mqtt_client.state());
    g_mqtt_connected = false;
    return false;
}

/**
 * 发布设备状态到 MQTT（JSON 格式心跳）
 *
 * 包含字段：uptime（秒）、rssi（信号强度）、heap（可用堆内存）
 */
inline void mqtt_publish_status() {
    // 关键修复：实际连接状态可能与标志位不一致（broker 静默断开），用 connected() 同步
    g_mqtt_connected = g_mqtt_client.connected();
    if (!g_mqtt_connected) return;

    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"uptime\":%lu,\"rssi\":%d,\"heap\":%u}",
             millis() / 1000, WiFi.RSSI(), ESP.getFreeHeap());

    bool ok = g_mqtt_client.publish(g_mqtt_topic_sta, payload);
    if (ok) {
        Serial.printf("[MQTT] 已发送遥测 → %s\n", payload);
    } else {
        Serial.println(F("[MQTT] 遥测发布失败！"));
    }
    g_mqtt_connected = g_mqtt_client.connected();
}

/**
 * MQTT 配置交互菜单
 *
 * 允许用户修改 Broker 地址、端口、发布主题、订阅主题并保存到 EEPROM
 */
inline void mqtt_config_menu() {
    Serial.println();
    Serial.printf("  当前 MQTT: %s:%d\n", g_mqtt_server, g_mqtt_port);
    if (g_mqtt_user[0]) {
        Serial.printf("  Token: %s\n", g_mqtt_user);
    }
    Serial.printf("  发布: %s  订阅: %s\n\n", g_mqtt_topic_sta, g_mqtt_topic_cmd);
    serial_flush_input();

    char buf[MAX_HOST_LEN + 1];

    Serial.print(F("Broker 地址 > "));
    if (serial_read_line(buf, sizeof(buf), 30000) && buf[0])
        strncpy(g_mqtt_server, buf, MAX_HOST_LEN);

    Serial.print(F("端口 > "));
    memset(buf, 0, sizeof(buf));
    if (serial_read_line(buf, sizeof(buf), 30000) && buf[0]) {
        int p = atoi(buf);
        if (p > 0 && p < 65536) g_mqtt_port = (uint16_t)p;
    }

    Serial.print(F("Access Token > "));
    memset(buf, 0, sizeof(buf));
    if (serial_read_line(buf, MAX_MQTT_USER_LEN + 1, 30000) && buf[0])
        strncpy(g_mqtt_user, buf, MAX_MQTT_USER_LEN);

    Serial.print(F("发布主题 > "));
    memset(buf, 0, sizeof(buf));
    if (serial_read_line(buf, sizeof(buf), 30000) && buf[0])
        strncpy(g_mqtt_topic_sta, buf, MAX_TOPIC_LEN);

    Serial.print(F("订阅主题 > "));
    memset(buf, 0, sizeof(buf));
    if (serial_read_line(buf, sizeof(buf), 30000) && buf[0])
        strncpy(g_mqtt_topic_cmd, buf, MAX_TOPIC_LEN);

    eeprom_save_mqtt();
    Serial.println(F("[MQTT 已保存]\n"));
}

#endif // MQTT_MANAGER_H
