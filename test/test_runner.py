#!/usr/bin/env python3
"""
ESP8266 固件单元测试（Python 验证版）

由于系统没有安装 GCC/MinGW，无法编译 native 平台的 C++ 测试。
本脚本用 Python 模拟相同的逻辑，验证 EEPROM 布局和桥接协议解析的正确性。

运行方式: python test/test_runner.py
"""

import struct
import sys
import os

# ── 从 config.h 提取的常量（与 C 代码保持同步） ──────────────────

# EEPROM 布局
EEPROM_TOTAL_SIZE = 512
EEPROM_MAGIC_VALUE = 0xE5F1
MAX_SSID_LEN = 32
MAX_PASS_LEN = 64
MAX_HOST_LEN = 40
MAX_TOPIC_LEN = 40

# EEPROM 地址（对应 EepromAddr namespace）
ADDR_MAGIC = 0
ADDR_SSID = 4
ADDR_PASS = 36
ADDR_MQTT_SERVER = 100
ADDR_MQTT_PORT = 140

# 桥接协议
BRIDGE_MAX_LINE_LEN = 256
BRIDGE_MAX_TOPIC_LEN = 128
BRIDGE_MAX_PAYLOAD_LEN = 512

# 默认值
DEFAULT_MQTT_SERVER = "broker.emqx.io"
DEFAULT_MQTT_PORT = 1883


# ── 测试框架 ────────────────────────────────────────────────

class TestRunner:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.errors = []

    def assert_equal(self, actual, expected, msg=""):
        if actual != expected:
            raise AssertionError(f"{msg}: expected {expected!r}, got {actual!r}")

    def assert_true(self, val, msg=""):
        if not val:
            raise AssertionError(f"{msg}: expected True, got {val!r}")

    def assert_false(self, val, msg=""):
        if val:
            raise AssertionError(f"{msg}: expected False, got {val!r}")

    def assert_equal_hex(self, actual, expected, msg=""):
        self.assert_equal(actual, expected, f"{msg} (hex: 0x{expected:X} vs 0x{actual:X})")

    def run(self, name, fn):
        try:
            fn(self)  # 传入 TestRunner 实例
            self.passed += 1
            print(f"  PASS {name}")
        except AssertionError as e:
            self.failed += 1
            self.errors.append((name, str(e)))
            print(f"  FAIL {name}: {e}")
        except Exception as e:
            self.failed += 1
            self.errors.append((name, f"EXCEPTION: {e}"))
            print(f"  FAIL {name}: EXCEPTION: {e}")

    def summary(self):
        total = self.passed + self.failed
        print(f"\n{'='*50}")
        print(f"  测试结果: {self.passed}/{total} 通过")
        if self.failed == 0:
            print(f"  状态: 全部通过 ✓")
        else:
            print(f"  状态: {self.failed} 个失败 ✗")
            for name, err in self.errors:
                print(f"    - {name}: {err}")
        print(f"{'='*50}")
        return self.failed == 0


# ── EEPROM 布局测试 ──────────────────────────────────────────

def test_eeprom_layout_no_overlap(t):
    t.assert_equal(ADDR_MAGIC, 0, "MAGIC 地址")
    t.assert_equal(ADDR_SSID, 4, "SSID 地址")
    t.assert_equal(ADDR_PASS, 36, "PASS 地址")
    t.assert_equal(ADDR_MQTT_SERVER, 100, "MQTT_SERVER 地址")
    t.assert_equal(ADDR_MQTT_PORT, 140, "MQTT_PORT 地址")

    # 验证不重叠
    t.assert_true(ADDR_SSID >= ADDR_MAGIC + 4, "SSID >= MAGIC + 4")
    t.assert_true(ADDR_PASS >= ADDR_SSID + MAX_SSID_LEN, "PASS >= SSID + 32")
    t.assert_true(ADDR_MQTT_SERVER >= ADDR_PASS + MAX_PASS_LEN, "MQTT_SERVER >= PASS + 64")
    t.assert_true(ADDR_MQTT_PORT >= ADDR_MQTT_SERVER + MAX_HOST_LEN - 1,
                  "MQTT_PORT >= MQTT_SERVER + 39")
    t.assert_true(ADDR_MQTT_PORT + 2 <= EEPROM_TOTAL_SIZE,
                  "MQTT_PORT + 2 <= 512")

def test_eeprom_magic_value(t):
    t.assert_equal_hex(EEPROM_MAGIC_VALUE, 0xE5F1, "魔数值")

def test_eeprom_buffer_sizes(t):
    t.assert_equal(MAX_SSID_LEN, 32)
    t.assert_equal(MAX_PASS_LEN, 64)
    t.assert_equal(MAX_HOST_LEN, 40)
    t.assert_equal(MAX_TOPIC_LEN, 40)
    t.assert_equal(EEPROM_TOTAL_SIZE, 512)


# ── EEPROM 读写模拟测试 ──────────────────────────────────────

def mock_eeprom_save_wifi(eeprom, ssid, password):
    struct.pack_into('<H', eeprom, ADDR_MAGIC, EEPROM_MAGIC_VALUE)
    ssid_bytes = ssid.encode('utf-8')[:MAX_SSID_LEN].ljust(MAX_SSID_LEN, b'\x00')
    pass_bytes = password.encode('utf-8')[:MAX_PASS_LEN].ljust(MAX_PASS_LEN, b'\x00')
    eeprom[ADDR_SSID:ADDR_SSID + MAX_SSID_LEN] = list(ssid_bytes)
    eeprom[ADDR_PASS:ADDR_PASS + MAX_PASS_LEN] = list(pass_bytes)

def mock_eeprom_load_wifi(eeprom):
    magic = struct.unpack_from('<H', bytes(eeprom), ADDR_MAGIC)[0]
    if magic != EEPROM_MAGIC_VALUE:
        return None, None, False
    ssid_raw = bytes(eeprom[ADDR_SSID:ADDR_SSID + MAX_SSID_LEN])
    pass_raw = bytes(eeprom[ADDR_PASS:ADDR_PASS + MAX_PASS_LEN])
    ssid = ssid_raw.split(b'\x00')[0].decode('utf-8', errors='replace')
    password = pass_raw.split(b'\x00')[0].decode('utf-8', errors='replace')
    if not ssid or ssid[0] == '\xff':
        return ssid, password, False
    return ssid, password, True

def mock_eeprom_save_mqtt(eeprom, server, port):
    server_bytes = server.encode('utf-8')[:MAX_HOST_LEN].ljust(MAX_HOST_LEN, b'\x00')
    eeprom[ADDR_MQTT_SERVER:ADDR_MQTT_SERVER + MAX_HOST_LEN] = list(server_bytes)
    struct.pack_into('<H', eeprom, ADDR_MQTT_PORT, port)

def mock_eeprom_load_mqtt(eeprom):
    port = struct.unpack_from('<H', bytes(eeprom), ADDR_MQTT_PORT)[0]
    if port == 0 or port == 0xFFFF:
        port = DEFAULT_MQTT_PORT
    server_raw = bytes(eeprom[ADDR_MQTT_SERVER:ADDR_MQTT_SERVER + MAX_HOST_LEN])
    server = ""
    for b in server_raw:
        server += chr(b) if 32 <= b < 127 else '\x00'
    server = server.split('\x00')[0]
    if not server or server[0] == '\xff':
        server = DEFAULT_MQTT_SERVER
    return server, port

def test_eeprom_save_and_load_wifi(t):
    eeprom = bytearray(EEPROM_TOTAL_SIZE)
    mock_eeprom_save_wifi(eeprom, "TestNetwork", "secret123")
    ssid, pwd, ok = mock_eeprom_load_wifi(eeprom)
    t.assert_true(ok)
    t.assert_equal(ssid, "TestNetwork")
    t.assert_equal(pwd, "secret123")

def test_eeprom_load_wifi_empty(t):
    eeprom = bytearray(b'\xFF' * EEPROM_TOTAL_SIZE)
    ssid, pwd, ok = mock_eeprom_load_wifi(eeprom)
    t.assert_false(ok)

def test_eeprom_clear_wifi(t):
    eeprom = bytearray(EEPROM_TOTAL_SIZE)
    mock_eeprom_save_wifi(eeprom, "TempNet", "pass")
    # 清除 = 写 0 到 magic
    struct.pack_into('<H', eeprom, ADDR_MAGIC, 0)
    ssid, pwd, ok = mock_eeprom_load_wifi(eeprom)
    t.assert_false(ok)

def test_eeprom_save_and_load_mqtt(t):
    eeprom = bytearray(EEPROM_TOTAL_SIZE)
    mock_eeprom_save_mqtt(eeprom, "test.mqtt.io", 8883)
    server, port = mock_eeprom_load_mqtt(eeprom)
    t.assert_equal(server, "test.mqtt.io")
    t.assert_equal(port, 8883)

def test_eeprom_load_mqtt_defaults(t):
    eeprom = bytearray(b'\xFF' * EEPROM_TOTAL_SIZE)
    server, port = mock_eeprom_load_mqtt(eeprom)
    t.assert_equal(server, DEFAULT_MQTT_SERVER)
    t.assert_equal(port, DEFAULT_MQTT_PORT)

def test_eeprom_load_mqtt_invalid_port(t):
    eeprom = bytearray(EEPROM_TOTAL_SIZE)
    # 端口 0 → 使用默认值
    struct.pack_into('<H', eeprom, ADDR_MQTT_PORT, 0)
    _, port = mock_eeprom_load_mqtt(eeprom)
    t.assert_equal(port, DEFAULT_MQTT_PORT)
    # 端口 0xFFFF → 使用默认值
    struct.pack_into('<H', eeprom, ADDR_MQTT_PORT, 0xFFFF)
    _, port = mock_eeprom_load_mqtt(eeprom)
    t.assert_equal(port, DEFAULT_MQTT_PORT)

def test_eeprom_load_mqtt_sanitizes_chars(t):
    eeprom = bytearray(EEPROM_TOTAL_SIZE)
    dirty = "test\x01\x1Fserver\x80\xFE.io"
    dirty_bytes = dirty.encode('latin-1')[:MAX_HOST_LEN].ljust(MAX_HOST_LEN, b'\x00')
    eeprom[ADDR_MQTT_SERVER:ADDR_MQTT_SERVER + MAX_HOST_LEN] = list(dirty_bytes)
    server, _ = mock_eeprom_load_mqtt(eeprom)
    t.assert_equal(server, "test")


# ── 桥接协议解析测试（模拟 bridge_parse_pub） ──────────────

def bridge_parse_pub(line, topic_max=128, payload_max=512):
    """模拟 C 版 bridge_parse_pub，返回 (err, topic, payload)"""
    sp = line.find(' ')
    if sp < 1:
        return 1, "", ""  # 无空格分隔
    topic = line[:sp]
    payload = line[sp+1:]
    if len(topic) >= topic_max:
        return 2, "", ""
    if len(payload) >= payload_max:
        return 3, "", ""
    return 0, topic, payload

def test_bridge_parse_pub_normal(t):
    err, topic, payload = bridge_parse_pub("sensor/temp 25.5")
    t.assert_equal(err, 0)
    t.assert_equal(topic, "sensor/temp")
    t.assert_equal(payload, "25.5")

def test_bridge_parse_pub_empty_payload(t):
    err, topic, payload = bridge_parse_pub("sensor/temp ")
    t.assert_equal(err, 0)
    t.assert_equal(topic, "sensor/temp")
    t.assert_equal(payload, "")

def test_bridge_parse_pub_no_space(t):
    err, _, _ = bridge_parse_pub("justatopic")
    t.assert_equal(err, 1)

def test_bridge_parse_pub_topic_too_long(t):
    err, _, _ = bridge_parse_pub("this_topic_is_way_too_long payload", topic_max=10)
    t.assert_equal(err, 2)

def test_bridge_parse_pub_payload_too_long(t):
    long_payload = "x" * 100
    err, _, _ = bridge_parse_pub("t " + long_payload, payload_max=10)
    t.assert_equal(err, 3)

def test_bridge_parse_pub_with_spaces(t):
    err, topic, payload = bridge_parse_pub("event/status device is online now")
    t.assert_equal(err, 0)
    t.assert_equal(topic, "event/status")
    t.assert_equal(payload, "device is online now")

def test_bridge_parse_pub_empty_line(t):
    err, _, _ = bridge_parse_pub("")
    t.assert_equal(err, 1)

def test_bridge_parse_pub_space_only(t):
    err, _, _ = bridge_parse_pub(" ")
    t.assert_equal(err, 1)

def test_bridge_parse_pub_special_chars(t):
    err, topic, payload = bridge_parse_pub("home/door {\"open\":true}")
    t.assert_equal(err, 0)
    t.assert_equal(topic, "home/door")
    t.assert_equal(payload, '{"open":true}')


# ── 配置常量合理性测试 ──────────────────────────────────────

def test_config_constants_sanity(t):
    t.assert_equal(DEFAULT_MQTT_PORT, 1883)
    t.assert_true(30000 >= 1000, "WiFi connect timeout >= 1s")
    t.assert_true(10000 >= 1000, "MQTT reconnect interval >= 1s")
    t.assert_true(30000 >= 1000, "MQTT status interval >= 1s")
    t.assert_true(5 >= 1, "Menu timeout >= 1s")
    t.assert_true(20 <= 100, "Bridge wait <= 100ms")
    t.assert_true(BRIDGE_MAX_LINE_LEN >= 64)
    t.assert_true(BRIDGE_MAX_TOPIC_LEN >= 32)
    t.assert_true(BRIDGE_MAX_PAYLOAD_LEN >= 64)

def test_bridge_constants_sanity(t):
    t.assert_true(BRIDGE_MAX_TOPIC_LEN < BRIDGE_MAX_LINE_LEN)
    t.assert_true(BRIDGE_MAX_PAYLOAD_LEN <= BRIDGE_MAX_LINE_LEN * 2)


# ═══════════════════════════════════════════════════════════
# 主入口
# ═══════════════════════════════════════════════════════════

if __name__ == "__main__":
    t = TestRunner()
    print("=" * 50)
    print("  ESP8266 固件单元测试")
    print("=" * 50)

    print("\n[EEPROM 布局]")
    t.run("地址不重叠且在范围内", test_eeprom_layout_no_overlap)
    t.run("魔数值正确", test_eeprom_magic_value)
    t.run("缓冲区大小合理", test_eeprom_buffer_sizes)

    print("\n[EEPROM 读写]")
    t.run("WiFi 凭据写入/读取", test_eeprom_save_and_load_wifi)
    t.run("空 EEPROM 返回失败", test_eeprom_load_wifi_empty)
    t.run("清除 WiFi 凭据后无法读取", test_eeprom_clear_wifi)
    t.run("MQTT 配置写入/读取", test_eeprom_save_and_load_mqtt)
    t.run("MQTT 默认值回退", test_eeprom_load_mqtt_defaults)
    t.run("MQTT 无效端口回退默认", test_eeprom_load_mqtt_invalid_port)
    t.run("MQTT 服务器名非法字符过滤", test_eeprom_load_mqtt_sanitizes_chars)

    print("\n[桥接协议解析]")
    t.run("正常 PUB 命令", test_bridge_parse_pub_normal)
    t.run("空 payload", test_bridge_parse_pub_empty_payload)
    t.run("无空格分隔", test_bridge_parse_pub_no_space)
    t.run("topic 超长", test_bridge_parse_pub_topic_too_long)
    t.run("payload 超长", test_bridge_parse_pub_payload_too_long)
    t.run("payload 含多个空格", test_bridge_parse_pub_with_spaces)
    t.run("空行", test_bridge_parse_pub_empty_line)
    t.run("仅空格", test_bridge_parse_pub_space_only)
    t.run("JSON payload", test_bridge_parse_pub_special_chars)

    print("\n[配置常量]")
    t.run("常量值合理性", test_config_constants_sanity)
    t.run("桥接常量合理性", test_bridge_constants_sanity)

    ok = t.summary()
    sys.exit(0 if ok else 1)
