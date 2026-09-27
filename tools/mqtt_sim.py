#!/usr/bin/env python3
"""MQTT simulator of the fan controller (ETH_Touch_PWM).

Behaves like the planned MQTT firmware, so the Home Assistant side (discovery,
entities, commands, the network-temperature automation) can be tested without
a board. Topics and rules: docs/MQTT.md. Fan logic mirrors src/sensors.cpp.

Setup:  pip install paho-mqtt
        Copy tools/mqtt_secrets.example.json to tools/mqtt_secrets.json
        (git-ignored) and fill in the password, or type it when asked.
Run:    python tools/mqtt_sim.py [--node fanController_sim] [--fans 2]
Remove: python tools/mqtt_sim.py --remove   (deletes the device from HA)

While running, type a line + Enter:
  l = toggle local probe fault, 1-4 = toggle fan N fault, q = quit
"""
import argparse
import getpass
import json
import math
import os
import random
import re
import socket
import sys
import threading
import time

import paho.mqtt.client as mqtt

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_NODE = "fanController_xx"  # Firmware default: MQTT stays off until the user changes it
NETWORK_STALE_S = 300              # No network temperature for this long -> network probe failed
TELEMETRY_S = 5


class Device:
    def __init__(self, node, fans):
        self.base = node
        self.node = node
        self.fans = fans
        self.t_min = 26.7            # config.cpp defaults, Celsius
        self.t_max = 37.8
        self.override = False
        self.override_duty = 255     # 0-255 like manualOverrideDutyCycle
        self.local_fault = False
        self.local_c = 30.0
        self.network_c = None
        self.network_at = 0.0
        self.blended = None
        self.fan_fault = [False] * fans
        self.duty = [0] * fans
        self.rpm = [0] * fans
        self.start = time.time()
        self.lock = threading.Lock()

    def t(self, suffix):
        return f"{self.base}/{suffix}"

    # ------------------------------------------------------------------ discovery

    def entities(self):
        temp = {"device_class": "temperature", "unit_of_measurement": "°C",
                "state_class": "measurement", "suggested_display_precision": 1}
        threshold = {"min": 0, "max": 100, "step": 0.1, "mode": "box",
                     "device_class": "temperature", "unit_of_measurement": "°C"}
        e = [
            ("sensor", "local_temp", {"name": "Local temperature", **temp}),
            ("sensor", "network_temp", {"name": "Network temperature", **temp}),
            ("sensor", "blended_temp", {"name": "Blended temperature", **temp}),
            ("binary_sensor", "local_probe_fault", {"name": "Local probe fault", "device_class": "problem"}),
            ("binary_sensor", "network_probe_fault", {"name": "Network probe fault", "device_class": "problem"}),
            ("number", "t_min", {"name": "Min threshold", "icon": "mdi:thermometer-low", **threshold}),
            ("number", "t_max", {"name": "Max threshold", "icon": "mdi:thermometer-high", **threshold}),
            ("switch", "override", {"name": "Manual override", "icon": "mdi:hand-back-right"}),
            ("number", "override_speed", {"name": "Override speed", "min": 0, "max": 100, "step": 1,
                                          "mode": "slider", "unit_of_measurement": "%", "icon": "mdi:fan"}),
            ("sensor", "daily_summary", {"name": "Daily summary", "icon": "mdi:calendar-today",
                                         "json_attributes_topic": self.t("daily_summary/attributes")}),
            ("sensor", "ip", {"name": "IP address", "icon": "mdi:ip-network", "entity_category": "diagnostic"}),
            ("sensor", "uptime", {"name": "Uptime", "unit_of_measurement": "s", "device_class": "duration",
                                  "entity_category": "diagnostic"}),
        ]
        for i in range(1, self.fans + 1):
            e += [
                ("sensor", f"fan{i}_rpm", {"name": f"Fan {i} speed", "unit_of_measurement": "RPM",
                                           "state_class": "measurement", "icon": "mdi:fan"}),
                ("sensor", f"fan{i}_duty", {"name": f"Fan {i} duty", "unit_of_measurement": "%",
                                            "state_class": "measurement", "icon": "mdi:fan-chevron-up"}),
                ("binary_sensor", f"fan{i}_fault", {"name": f"Fan {i} fault", "device_class": "problem"}),
            ]
        return e

    def discovery(self):
        msgs = []
        for component, obj, cfg in self.entities():
            cfg = dict(cfg)
            cfg["unique_id"] = f"{self.node}_{obj}"
            cfg["state_topic"] = self.t(obj)
            if component in ("number", "switch"):
                cfg["command_topic"] = self.t(f"{obj}/set")
            cfg["availability_topic"] = self.t("status")
            cfg["device"] = {"identifiers": [self.node], "name": self.node, "manufacturer": "DIY",
                             "model": "ETH Touch PWM fan controller (simulator)", "sw_version": "sim"}
            msgs.append((f"homeassistant/{component}/{self.node}/{obj}/config", json.dumps(cfg)))
        return msgs

    # ------------------------------------------------------------------ behaviour

    def network_ok(self):
        return self.network_c is not None and time.time() - self.network_at < NETWORK_STALE_S

    def step(self):
        # Slow swing across the default thresholds so the fan curve moves
        t = time.time() - self.start
        self.local_c = 31.0 + 5.0 * math.sin(t / 120.0) + random.uniform(-0.1, 0.1)
        local_ok = not self.local_fault
        net_ok = self.network_ok()

        # evaluateSensorFailsafes()
        if local_ok and net_ok:
            self.blended = (self.local_c + self.network_c) / 2.0
        elif local_ok:
            self.blended = self.local_c
        elif net_ok:
            self.blended = self.network_c
        else:
            self.blended = None

        # calculateFanCurve(); blackout -> full speed (the intended failsafe)
        if self.blended is None:
            duty = 255
        elif self.override:
            duty = self.override_duty
        elif self.t_max <= self.t_min:
            duty = 255
        elif self.blended < self.t_min:
            duty = 0
        elif self.blended >= self.t_max:
            duty = 255
        else:
            duty = int(51.0 + (self.blended - self.t_min) / (self.t_max - self.t_min) * 204.0)

        for i in range(self.fans):
            self.duty[i] = duty
            if self.fan_fault[i] or duty == 0:
                self.rpm[i] = 0
            else:
                self.rpm[i] = int(400 + duty / 255.0 * 1800 + random.uniform(-15, 15))

    def states(self):
        def num(v):
            return "None" if v is None else f"{v:.1f}"  # "None" = unknown in HA

        net_ok = self.network_ok()
        s = [
            ("local_temp", num(None if self.local_fault else self.local_c)),
            ("network_temp", num(self.network_c if net_ok else None)),
            ("blended_temp", num(self.blended)),
            ("local_probe_fault", "ON" if self.local_fault else "OFF"),
            ("network_probe_fault", "OFF" if net_ok else "ON"),
            ("t_min", f"{self.t_min:.1f}"),
            ("t_max", f"{self.t_max:.1f}"),
            ("override", "ON" if self.override else "OFF"),
            ("override_speed", str(round(self.override_duty * 100 / 255))),
            ("ip", "simulator"),
            ("uptime", str(int(time.time() - self.start))),
        ]
        for i in range(self.fans):
            s += [
                (f"fan{i + 1}_rpm", str(self.rpm[i])),
                (f"fan{i + 1}_duty", str(round(self.duty[i] * 100 / 255))),
                (f"fan{i + 1}_fault", "ON" if self.duty[i] > 51 and self.rpm[i] == 0 else "OFF"),
            ]
        return [(self.t(k), v) for k, v in s]

    def daily_summary(self):
        # Fake rollup for yesterday, shaped like pushDailyRollupToHA()
        day = time.strftime("%Y-%m-%d", time.localtime(time.time() - 86400))
        attrs = {"local_min_c": 24.3, "local_max_c": 35.8, "net_min_c": 22.9, "net_max_c": 27.4,
                 "blend_min_c": 23.8, "blend_max_c": 31.2}
        for i in range(1, self.fans + 1):
            attrs[f"fan{i}_min_rpm"] = 0
            attrs[f"fan{i}_max_rpm"] = 2150
        return [(self.t("daily_summary"), day), (self.t("daily_summary/attributes"), json.dumps(attrs))]

    def command(self, obj, payload):
        if obj in ("t_min", "t_max"):
            try:
                v = float(payload)
            except ValueError:
                return f"bad value {payload!r}"
            if not 0 <= v <= 100:
                return f"out of range {v}"
            setattr(self, obj, v)
            return f"{obj} = {v:.1f} C"
        if obj == "override":
            if payload not in ("ON", "OFF"):
                return f"bad value {payload!r}"
            self.override = payload == "ON"
            if self.override:
                self.override_duty = 255  # Engaging starts at full speed, like the web page and LCD
            return f"override {payload}"
        if obj == "override_speed":
            if not self.override:
                return "override speed ignored: override is off"
            try:
                pct = max(0, min(100, round(float(payload))))
            except ValueError:
                return f"bad value {payload!r}"
            self.override_duty = round(pct * 255 / 100)
            return f"override speed {pct} %"
        if obj == "network_temp":
            try:
                d = json.loads(payload)
                v = float(d["value"])
                unit = str(d.get("unit") or "")
            except (ValueError, KeyError, TypeError):
                self.network_c = None
                return f"network temperature unavailable ({payload})"
            self.network_c = (v - 32.0) * 5.0 / 9.0 if "F" in unit else v
            self.network_at = time.time()
            return f"network temperature {v} {unit} = {self.network_c:.1f} C"
        return f"unknown command {obj}"


def make_client(client_id):
    try:  # paho-mqtt 2.x
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=client_id)
    except AttributeError:  # paho-mqtt 1.x
        return mqtt.Client(client_id=client_id)


def load_secrets():
    path = os.path.join(HERE, "mqtt_secrets.json")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    return {}


def main():
    secrets = load_secrets()
    ap = argparse.ArgumentParser(description="Fan controller MQTT simulator")
    ap.add_argument("--node", default="fanController_sim", help="nodeID (device name in HA)")
    ap.add_argument("--fans", type=int, default=2, choices=range(1, 5))
    ap.add_argument("--host", default=secrets.get("host", "192.168.10.85"))
    ap.add_argument("--port", type=int, default=secrets.get("port", 1883))
    ap.add_argument("--user", default=secrets.get("username", ""))
    ap.add_argument("--remove", action="store_true", help="delete the simulated device from HA and exit")
    args = ap.parse_args()

    if not re.fullmatch(r"[A-Za-z0-9_-]{1,63}", args.node) or args.node == DEFAULT_NODE:
        sys.exit(f"Node ID {args.node!r} not allowed: letters, digits, _ and - only, and not {DEFAULT_NODE}")

    password = secrets.get("password")
    if args.user and password is None:
        password = getpass.getpass(f"MQTT password for {args.user}: ")

    dev = Device(args.node, args.fans)
    client = make_client(args.node)
    if args.user:
        client.username_pw_set(args.user, password)
    client.will_set(dev.t("status"), "offline", qos=1, retain=True)
    connected = threading.Event()

    def on_connect(c, userdata, flags, rc):
        if rc != 0:
            print(f"[MQTT] Connect refused: {mqtt.connack_string(rc)}")
            return
        print(f"[MQTT] Connected to {args.host}:{args.port} as {args.node}")
        connected.set()
        if args.remove:
            return
        c.publish(dev.t("status"), "online", qos=1, retain=True)
        for topic, payload in dev.discovery():
            c.publish(topic, payload, qos=1, retain=True)
        c.subscribe(dev.t("+/set"))
        with dev.lock:
            dev.step()
            msgs = dev.states() + dev.daily_summary()
        for topic, payload in msgs:
            c.publish(topic, payload, retain=True)
        print("[MQTT] Discovery + states published")

    def on_disconnect(c, userdata, rc):
        connected.clear()
        if rc != 0:
            print(f"[MQTT] Disconnected (rc {rc}), retrying")

    def on_message(c, userdata, msg):
        obj = msg.topic[len(dev.base) + 1:-len("/set")]
        payload = msg.payload.decode("utf-8", "replace").strip()
        with dev.lock:
            result = dev.command(obj, payload)
            dev.step()
            msgs = dev.states()
        print(f"[CMD] {msg.topic} = {payload} -> {result}")
        for topic, p in msgs:
            c.publish(topic, p, retain=True)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    try:
        client.connect(args.host, args.port, keepalive=30)
    except (OSError, socket.timeout) as e:
        sys.exit(f"Cannot reach {args.host}:{args.port}: {e}")
    client.loop_start()
    if not connected.wait(10):
        client.loop_stop()
        sys.exit("No connection (see message above)")

    if args.remove:
        topics = [t for t, _ in dev.discovery()] + [t for t, _ in dev.states() + dev.daily_summary()]
        topics.append(dev.t("status"))
        for t in topics:
            client.publish(t, "", qos=1, retain=True).wait_for_publish()
        client.disconnect()
        client.loop_stop()
        print(f"Removed {args.node} from Home Assistant (retained discovery and states cleared)")
        return

    stop = threading.Event()

    def keyboard():
        for line in sys.stdin:
            key = line.strip().lower()
            with dev.lock:
                if key == "l":
                    dev.local_fault = not dev.local_fault
                    print(f"[SIM] local probe fault {'ON' if dev.local_fault else 'OFF'}")
                elif key in ("1", "2", "3", "4") and int(key) <= dev.fans:
                    i = int(key) - 1
                    dev.fan_fault[i] = not dev.fan_fault[i]
                    print(f"[SIM] fan {key} fault {'ON' if dev.fan_fault[i] else 'OFF'}")
                elif key == "q":
                    stop.set()
                    return

    threading.Thread(target=keyboard, daemon=True).start()
    try:
        while not stop.wait(TELEMETRY_S):
            if not connected.is_set():
                continue
            with dev.lock:
                dev.step()
                msgs = dev.states()
                summary = (f"local {dev.local_c:.1f} C, network "
                           f"{f'{dev.network_c:.1f} C' if dev.network_ok() else '--'}, "
                           f"duty {round(dev.duty[0] * 100 / 255)} %, rpm {dev.rpm}")
            for topic, payload in msgs:
                client.publish(topic, payload, retain=True)
            print(f"[SIM] {summary}")
    except KeyboardInterrupt:
        pass
    client.publish(dev.t("status"), "offline", qos=1, retain=True).wait_for_publish()
    client.disconnect()
    client.loop_stop()
    print("Stopped (device shows unavailable in HA)")


if __name__ == "__main__":
    main()
