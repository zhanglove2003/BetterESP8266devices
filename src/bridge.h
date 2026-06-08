/**
 * @file bridge.h
 * @brief STM32 串口桥接协议
 *
 * 处理来自 STM32（或其他主控）的串口命令。
 *
 * 协议格式：
 *   - 命令以 ">>" 开头（两个 '>' 字符）
 *   - 响应以 "<<" 开头
 *
 * 支持的命令：
 *   >>PUB <topic> <payload>    发布 MQTT 消息
 *   >>SUB <topic>              订阅 MQTT 主题
 *   >>INFO                     返回设备信息（IP/RSSI/HEAP/UPTIME）
 *   >>HELP                     显示帮助
 */

#pragma once
#ifndef BRIDGE_H
#define BRIDGE_H

#include "config.h"

/**
 * 解析桥接命令中的 PUB 参数（topic + payload）
 *
 * 纯逻辑函数，不依赖硬件，可用于单元测试。
 *
 * @param line     命令行（已去除 "PUB " 前缀）
 * @param topic    输出 topic 缓冲区
 * @param topic_max topic 缓冲区最大长度
 * @param payload 输出 payload 缓冲区
 * @param payload_max payload 缓冲区最大长度
 * @return int     0=成功, 1=无空格分隔, 2=topic超长, 3=payload超长
 */
inline int bridge_parse_pub(const char *line, char *topic, size_t topic_max,
                           char *payload, size_t payload_max) {
    const char *sp = strchr(line, ' ');
    if (!sp || sp == line) return 1;  // 无 payload 部分（仅 topic 也允许）

    size_t topic_len = (size_t)(sp - line);
    if (topic_len == 0) return 1;

    if (topic_len >= topic_max) return 2;  // topic 超长
    memcpy(topic, line, topic_len);
    topic[topic_len] = '\0';

    const char *pl = sp + 1;
    size_t pl_len = strlen(pl);
    if (pl_len >= payload_max) return 3;  // payload 超长
    memcpy(payload, pl, pl_len);
    payload[pl_len] = '\0';

    return 0;
}

/**
 * 处理一条 STM32 桥接命令（已在 loop() 中检测到 ">>" 前缀后调用）
 *
 * @note 此函数会阻塞读取串口直到收到换行符
 *       读取长度受 BRIDGE_MAX_LINE_LEN 限制
 */
inline void handle_bridge_command() {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) {
        Serial.println(F("<<ERR empty"));
        return;
    }

    // 长度保护：超过限制直接拒绝
    if (line.length() > BRIDGE_MAX_LINE_LEN) {
        Serial.println(F("<<ERR too long"));
        return;
    }

    if (line.startsWith("PUB ")) {
        // 解析 "PUB <topic> <payload>"
        if (!g_mqtt_connected || !g_mqtt_client.connected()) {
            Serial.println(F("<<ERR no mqtt"));
            g_mqtt_connected = false;
            return;
        }
        char topic[BRIDGE_MAX_TOPIC_LEN];
        char payload[BRIDGE_MAX_PAYLOAD_LEN];
        int err = bridge_parse_pub(line.c_str() + 4, topic, sizeof(topic),
                                    payload, sizeof(payload));
        if (err == 0) {
            g_mqtt_client.publish(topic, payload);
            g_mqtt_connected = g_mqtt_client.connected();
            Serial.printf("<<OK PUB %s\n", topic);
        } else {
            Serial.printf("<<ERR parse:%d\n", err);
        }
    } else if (line.startsWith("SUB ") && g_mqtt_connected) {
        String topic = line.substring(4);
        if (topic.length() > BRIDGE_MAX_TOPIC_LEN) {
            Serial.println(F("<<ERR topic too long"));
        } else {
            g_mqtt_client.subscribe(topic.c_str());
            Serial.printf("<<OK SUB %s\n", topic.c_str());
        }
    } else if (line == "INFO") {
        // 返回设备状态信息
        Serial.printf("<<INFO IP=%s RSSI=%d HEAP=%u UPTIME=%lu\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI(),
                      ESP.getFreeHeap(),
                      millis() / 1000);
    } else if (line == "?" || line == "HELP") {
        Serial.println(F("<<HELP PUB|SUB|INFO"));
    } else {
        Serial.printf("<<ERR unknown: %s\n", line.c_str());
    }
}

/**
 * 检测串口输入是否为桥接命令前缀 ">>"
 *
 * 在 loop() 中每次读取到 '>' 字符时调用，
 * 检测第二个字符是否也是 '>'。
 *
 * @return true  检测到 ">>" 前缀，已自动调用 handle_bridge_command()
 */
inline bool check_bridge_prefix() {
    // 第一个 '>' 已在 loop() 中消费，等待第二个
    uint32_t w0 = millis();
    while (!Serial.available() && millis() - w0 < BRIDGE_WAIT_NEXT_CHAR_MS) {
        delay(1);
    }
    if (Serial.available() && Serial.read() == '>') {
        handle_bridge_command();
        return true;
    }
    return false;
}

#endif // BRIDGE_H
