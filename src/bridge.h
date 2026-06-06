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
 * 处理一条 STM32 桥接命令（已在 loop() 中检测到 ">>" 前缀后调用）
 *
 * @note 此函数会阻塞读取串口直到收到换行符
 */
inline void handle_bridge_command() {
    String line = Serial.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) {
        Serial.println(F("<<ERR empty"));
        return;
    }

    if (line.startsWith("PUB ")) {
        // 解析 "PUB <topic> <payload>"
        int sp = line.indexOf(' ', 4);
        if (sp > 4 && g_mqtt_connected) {
            String topic = line.substring(4, sp);
            String payload = line.substring(sp + 1);
            g_mqtt_client.publish(topic.c_str(), payload.c_str());
            Serial.printf("<<OK PUB %s\n", topic.c_str());
        } else {
            Serial.println(F("<<ERR"));
        }
    } else if (line.startsWith("SUB ") && g_mqtt_connected) {
        String topic = line.substring(4);
        g_mqtt_client.subscribe(topic.c_str());
        Serial.printf("<<OK SUB %s\n", topic.c_str());
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
