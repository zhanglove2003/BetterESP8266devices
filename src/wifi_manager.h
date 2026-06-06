/**
 * @file wifi_manager.h
 * @brief WiFi 连接管理
 *
 * 提供 WiFi 扫描选择连接和 EEPROM 自动重连两种模式。
 * 关键修复：WIFI_NONE_SLEEP 防止 7 秒断连。
 */

#pragma once
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "config.h"
#include "serial_utils.h"
#include "eeprom_store.h"

/**
 * 打印芯片硬件信息到串口（Chip ID、Flash 大小、堆内存、MAC 等）
 */
inline void print_chip_info() {
    Serial.println();
    Serial.println(F("  ESP8266 Firmware v3.0"));
    Serial.println(F("  ==============================="));
    Serial.printf("    Chip ID:    0x%06X\n", ESP.getChipId());
    Serial.printf("    Flash:      %u KB\n", ESP.getFlashChipRealSize() / 1024);
    Serial.printf("    Free Heap:  %u bytes\n", ESP.getFreeHeap());
    Serial.printf("    CPU Freq:   %u MHz\n", ESP.getCpuFreqMHz());
    uint8_t mac[6];
    WiFi.macAddress(mac);
    Serial.printf("    MAC:        %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println();
}

/**
 * 扫描周围 WiFi 网络并让用户选择连接
 *
 * 交互流程：列出网络 → 用户选择 → 输入密码 → 连接 → 稳定性验证
 * 连接成功后自动保存凭据到 EEPROM
 */
inline void wifi_scan_and_connect() {
    Serial.println(F(">>> 扫描 WiFi..."));
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks(false, true);
    if (n <= 0) {
        Serial.println(F("  [失败] 未发现网络\n"));
        return;
    }

    Serial.printf("\n  发现 %d 个网络:\n", n);
    Serial.println(F("  --------------------------------------------"));
    for (int i = 0; i < n; i++) {
        const char *enc = (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "OPEN" : "ENC";
        Serial.printf("  %2d: %-25s CH:%2d %4d %s\n",
                      i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), enc);
    }
    Serial.println(F("  --------------------------------------------"));

    Serial.print(F("选择 (1~"));
    Serial.print(n);
    Serial.print(F("), r=刷新, 0=取消 > "));
    serial_flush_input();

scan_pick:
    int choice = -1;
    uint32_t t0 = millis();
    while (millis() - t0 < 120000) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == 'r' || c == 'R') {
                serial_flush_input();
                WiFi.scanNetworks(false, true);
                goto scan_pick;
            }
            String s;
            s += c;
            s += Serial.readStringUntil('\n');
            s.trim();
            choice = s.toInt();
            break;
        }
        delay(10);
    }
    if (choice <= 0 || choice > n) {
        Serial.println(F("[取消]\n"));
        return;
    }

    String ssid = WiFi.SSID(choice - 1);
    bool is_open = (WiFi.encryptionType(choice - 1) == ENC_TYPE_NONE);
    Serial.printf("已选择: %s\n", ssid.c_str());
    WiFi.scanDelete();

    String pass;
    if (!is_open) {
        Serial.print(F("输入密码 > "));
        char pw[MAX_PASS_LEN + 1] = {0};
        serial_read_line(pw, MAX_PASS_LEN + 1, 60000);
        if (pw[0] == '\0') Serial.println(F("(空密码)"));
        pass = pw;
    } else {
        pass = "";
        Serial.println(F("开放网络"));
    }

    Serial.printf("正在连接 \"%s\" ...\n", ssid.c_str());

    // 关键修复：禁用 WiFi 睡眠，防止 7 秒断连
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t conn_start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - conn_start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(200);
        yield();
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[失败] 无法连接 WiFi\n"));
        return;
    }

    // 连接稳定性验证（5 秒内保持连接）
    uint32_t stable = millis();
    bool ok = true;
    while (millis() - stable < WIFI_STABLE_CHECK_MS) {
        if (WiFi.status() != WL_CONNECTED) { ok = false; break; }
        delay(200);
    }
    if (!ok) {
        Serial.println(F("[失败] 连接不稳定\n"));
        return;
    }

    Serial.println(F("[成功] WiFi 连接稳定!"));
    Serial.printf("  IP: %s  信号: %d dBm\n\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

    strncpy(g_wifi_ssid, ssid.c_str(), MAX_SSID_LEN);
    strncpy(g_wifi_pass, pass.c_str(), MAX_PASS_LEN);
    g_wifi_configured = true;
    g_wifi_connected = true;
    eeprom_save_wifi();
}

/**
 * 使用 EEPROM 中保存的凭据自动连接 WiFi
 *
 * @return true  连接成功且稳定
 * @return false 无保存凭据或连接失败（自动清除无效凭据）
 */
inline bool wifi_auto_connect() {
    if (!eeprom_load_wifi()) return false;

    Serial.printf("自动连接 \"%s\" ...\n", g_wifi_ssid);

    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifi_ssid, g_wifi_pass);

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 60) {
        delay(500);
        retry++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        eeprom_clear_wifi();
        return false;
    }

    // 稳定性检查
    uint32_t t0 = millis();
    bool ok = true;
    while (millis() - t0 < 3000) {
        if (WiFi.status() != WL_CONNECTED) { ok = false; break; }
        delay(200);
    }
    if (!ok) return false;

    g_wifi_configured = true;
    g_wifi_connected = true;
    Serial.printf("[成功] IP: %s  信号: %d dBm\n\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

#endif // WIFI_MANAGER_H
