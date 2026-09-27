#include "web_server.h"
#include "config.h"
#include "sensors.h"
#include "home_assistant.h"
#include "sd_logger.h"
#include <TimeLib.h>
#include <LittleFS.h>
#include <math.h>

static String getUrlParam(String src, String param) {
    int idx = src.indexOf(param);
    if (idx == -1) return "";

    int start = idx + param.length();
    int end = src.indexOf('&', start);
    if (end == -1) end = src.length();

    String val = src.substring(start, end);
    val.replace("+", " ");

    String decoded = "";
    for (size_t i = 0; i < val.length(); i++) {
        if (val[i] == '%' && i + 2 < val.length()) {
            char high = val[i + 1];
            char low  = val[i + 2];
            int hVal = (high >= 'a') ? (high - 'a' + 10) : (high >= 'A') ? (high - 'A' + 10) : (high - '0');
            int lVal = (low  >= 'a') ? (low  - 'a' + 10) : (low  >= 'A') ? (low  - 'A' + 10) : (low  - '0');
            decoded += (char)((hVal << 4) | lVal);
            i += 2;
        } else {
            decoded += val[i];
        }
    }
    return decoded;
}

static void parseIpString(String ipStr, IPAddress &ip) {
    int p0 = 0, p1 = 0, p2 = 0, p3 = 0;
    if (sscanf(ipStr.c_str(), "%d.%d.%d.%d", &p0, &p1, &p2, &p3) == 4) {
        ip = IPAddress(p0, p1, p2, p3);
    } else {
        Serial.println("Warning: failed to parse IP address string.");
    }
}

void handleNativeWebTraffic(EthernetClient& client) {
    String req = client.readStringUntil('\r');

    // --- AJAX telemetry endpoint ---
    if (req.indexOf("GET /ajax_data") != -1) {
        client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n");

        float liveLocal   = config.isFahrenheit ? ((localTempC * 9.0 / 5.0) + 32.0) : localTempC;
        float liveNetwork = config.isFahrenheit ? ((networkTempC * 9.0 / 5.0) + 32.0) : networkTempC;
        float liveBlended = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;

        // "--" instead of a bare number when unhealthy - otherwise this
        // silently shows the untouched 0.0 startup default as if it were a
        // real reading, unlike the LCD which correctly shows an empty bar.
        String localStr   = localSensorHealthy ? String(liveLocal, 1) : "--";
        String networkStr = networkSensorHealthy ? String(liveNetwork, 1) : "--";
        String blendedStr = (localSensorHealthy || networkSensorHealthy) ? String(liveBlended, 1) : "--";

        String json = "{";
        json += "\"local_temp\":\"" + localStr + "\",";
        json += "\"net_temp\":\"" + networkStr + "\",";
        json += "\"blend_temp\":\"" + blendedStr + "\",";
        json += "\"override_active\":" + String(manualOverrideActive ? "true" : "false") + ",";
        json += "\"override_speed\":" + String(manualOverrideDutyCycle) + ",";
        json += "\"sd_present\":" + String(isSdCardPresent() ? "true" : "false") + ",";
        json += "\"fans\":[";
        for (int i = 0; i < 4; i++) {
            json += String(currentRPMs[i]);
            if (i < 3) json += ",";
        }
        json += "]}";

        client.println(json);
        delay(1);
        client.stop();
        return;
    }

    // --- Manual override control endpoint ---
    // Deliberately separate from /submit: override state is a live action
    // (mirrors the LCD touch UI / HA), not a persisted setting - it never
    // touches saveSettings()/LittleFS, per the boot-behavior rule that
    // override always starts false on every boot regardless of source.
    if (req.indexOf("GET /override_set") != -1) {
        bool wasActive = manualOverrideActive;
        int wasDuty = manualOverrideDutyCycle;

        String activeParam = getUrlParam(req, "active=");
        String speedParam  = getUrlParam(req, "speed=");

        // Check the first character rather than exact string equality:
        // when there's no following '&'-delimited param (e.g. a bare
        // "active=0" with nothing after it), getUrlParam() grabs everything
        // to the end of the raw request line, including trailing " HTTP/1.1" -
        // exact equality against "0" then silently fails.
        bool switchChanged = false;
        bool speedOnlyChanged = false;

        if (activeParam.length() > 0 && activeParam.charAt(0) == '1') {
            manualOverrideActive = true;
            manualOverrideDutyCycle = speedParam.length() > 0 ? constrain(speedParam.toInt(), 0, 255) : 255;
            switchChanged = true;
        } else if (activeParam.length() > 0 && activeParam.charAt(0) == '0') {
            manualOverrideActive = false;
            switchChanged = true;
        } else if (manualOverrideActive && speedParam.length() > 0) {
            // Active and only the slider moved - adjust speed without
            // touching the on/off state.
            manualOverrideDutyCycle = constrain(speedParam.toInt(), 0, 255);
            speedOnlyChanged = true;
        }

        // Switch-only on engage/disengage, speed-only on a genuine slider
        // move - never both together. See home_assistant.h for why: pushing
        // both let HA's own automations bounce a stale speed value back on
        // every switch change, since HA processes the two asynchronously.
        if (switchChanged) pushOverrideSwitchToHA();
        else if (speedOnlyChanged) pushOverrideSpeedToHA();

        int pct = (manualOverrideDutyCycle * 100) / 255;
        if (manualOverrideActive != wasActive) {
            Serial.print("Override "); Serial.print(manualOverrideActive ? "ACTIVATED" : "DEACTIVATED");
            Serial.print(" via web - speed="); Serial.print(pct); Serial.println("%");
            sdLogEvent("OVERRIDE", String("source=web action=") + (manualOverrideActive ? "ON" : "OFF") + " speed=" + String(pct) + "%");
        } else if (manualOverrideDutyCycle != wasDuty) {
            Serial.print("Override SPEED changed via web: "); Serial.print(pct); Serial.println("%");
            sdLogEvent("OVERRIDE", "source=web action=SPEED speed=" + String(pct) + "%");
        }

        client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n");
        client.println("{\"ok\":true}");
        delay(1);
        client.stop();
        return;
    }

    bool networkSettingsChanged = false;
    bool regularSettingsChanged = false;

    // --- Settings form submission ---
    if (req.indexOf("POST /submit") != -1 || req.indexOf("GET /submit?") != -1) {
        String body = "";
        if (req.indexOf("POST") != -1) {
            while (client.available()) {
                String line = client.readStringUntil('\n');
                if (line == "\r") {
                    body = client.readString();
                    break;
                }
            }
        } else {
            body = req;
        }

        String unitParam = getUrlParam(body, "unit=");
        bool submittedAsFahrenheit = (unitParam == "F");

        String tminStr = getUrlParam(body, "tmin=");
        String tmaxStr = getUrlParam(body, "tmax=");
        if (tminStr.length() > 0 && tmaxStr.length() > 0) {
            float oldTMin = config.tMin;
            float oldTMax = config.tMax;

            float parsedMin = tminStr.toFloat();
            float parsedMax = tmaxStr.toFloat();
            if (submittedAsFahrenheit) {
                config.tMin = (parsedMin - 32.0) * 5.0 / 9.0;
                config.tMax = (parsedMax - 32.0) * 5.0 / 9.0;
            } else {
                config.tMin = parsedMin;
                config.tMax = parsedMax;
            }

            // Log the change (values always stored/logged in Celsius,
            // matching internal representation - ignore float noise with
            // a small tolerance so re-submitting the same form doesn't
            // spam identical-looking rows).
            if (fabs(config.tMin - oldTMin) > 0.05) {
                sdLogEvent("CONFIG", "source=web field=tMin old=" + String(oldTMin, 1) + "C new=" + String(config.tMin, 1) + "C");
            }
            if (fabs(config.tMax - oldTMax) > 0.05) {
                sdLogEvent("CONFIG", "source=web field=tMax old=" + String(oldTMax, 1) + "C new=" + String(config.tMax, 1) + "C");
            }

            // Push immediately so HA's own stored value matches - otherwise
            // the next periodic poll (home_assistant.cpp: fetchThresholdsFromHA(),
            // every 15s) would see HA's stale value and silently revert
            // this change right back.
            pushThresholdsToHA();
        }

        config.isFahrenheit = submittedAsFahrenheit;
        config.is24Hour = (getUrlParam(body, "clk=") == "24");
        config.tzOffset = getUrlParam(body, "tz=").toInt();
        config.fanCount = getUrlParam(body, "fancnt=").toInt();

        String nodeParam = getUrlParam(body, "nodeid=");
        if (nodeParam.length() > 0) {
            nodeParam.replace(" ", "_");
            strncpy(config.nodeID, nodeParam.c_str(), sizeof(config.nodeID) - 1);
            config.nodeID[sizeof(config.nodeID) - 1] = '\0';
        }

        String hostParam = getUrlParam(body, "hahost=");
        if (hostParam.length() > 0) {
            hostParam.replace(" ", "");
            strncpy(config.haHost, hostParam.c_str(), sizeof(config.haHost) - 1);
            config.haHost[sizeof(config.haHost) - 1] = '\0';
        }

        String portParam = getUrlParam(body, "haport=");
        if (portParam.length() > 0) {
            int checkPort = portParam.toInt();
            if (checkPort > 0) config.haPort = checkPort;
        }

        String sensorParam = getUrlParam(body, "hasensor=");
        if (sensorParam.length() > 0) {
            sensorParam.replace(" ", "_");
            strncpy(config.haSensor, sensorParam.c_str(), sizeof(config.haSensor) - 1);
            config.haSensor[sizeof(config.haSensor) - 1] = '\0';
        }

        String tMinEntityParam = getUrlParam(body, "hatminentity=");
        if (tMinEntityParam.length() > 0) {
            strncpy(config.haTMinEntity, tMinEntityParam.c_str(), sizeof(config.haTMinEntity) - 1);
            config.haTMinEntity[sizeof(config.haTMinEntity) - 1] = '\0';
        }

        String tMaxEntityParam = getUrlParam(body, "hatmaxentity=");
        if (tMaxEntityParam.length() > 0) {
            strncpy(config.haTMaxEntity, tMaxEntityParam.c_str(), sizeof(config.haTMaxEntity) - 1);
            config.haTMaxEntity[sizeof(config.haTMaxEntity) - 1] = '\0';
        }

        String tokenParam = getUrlParam(body, "hatoken=");
        if (tokenParam.length() > 0) {
            strncpy(config.haToken, tokenParam.c_str(), sizeof(config.haToken) - 1);
            config.haToken[sizeof(config.haToken) - 1] = '\0';
        }

        String ipStr = getUrlParam(body, "ip=");
        if (ipStr.length() > 0) {
            parseIpString(ipStr, config.ip);
            parseIpString(getUrlParam(body, "sub="), config.subnet);
            parseIpString(getUrlParam(body, "gw="), config.gateway);
            parseIpString(getUrlParam(body, "dns="), config.dns);
            networkSettingsChanged = true;
        }

        saveSettings();
        regularSettingsChanged = true;
    }

    if (networkSettingsChanged || regularSettingsChanged) {
        client.println("HTTP/1.1 303 See Other");
        client.println("Location: /");
        client.println("Connection: close\r\n");
        client.stop();
        if (networkSettingsChanged) {
            // Give the flash controller real margin to finish committing
            // saveSettings()'s write, and cleanly unmount LittleFS, before
            // the hard reset. Restarting too soon after a write is the
            // likely cause of a past LittleFS corruption incident on this
            // exact code path (see config.cpp's mountLittleFSWithRecovery()
            // for the self-healing fallback if it happens again anyway).
            delay(1500);
            LittleFS.end();
            delay(200);
            ESP.restart();
        }
        return;
    }

    // --- Dashboard page ---
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Cache-Control: no-cache, no-store, must-revalidate");
    client.println("Pragma: no-cache");
    client.println("Expires: 0");
    client.println("Connection: close");
    client.println();

    float displayMin = config.isFahrenheit ? ((config.tMin * 9.0 / 5.0) + 32.0) : config.tMin;
    float displayMax = config.isFahrenheit ? ((config.tMax * 9.0 / 5.0) + 32.0) : config.tMax;
    String scaleSymbol = config.isFahrenheit ? "F" : "C";

    float initialLocal   = config.isFahrenheit ? ((localTempC * 9.0 / 5.0) + 32.0) : localTempC;
    float initialNetwork = config.isFahrenheit ? ((networkTempC * 9.0 / 5.0) + 32.0) : networkTempC;
    float initialBlended = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;

    // "--" instead of a bare number when unhealthy - see the matching fix
    // in the /ajax_data handler above for why this matters.
    String initialLocalStr   = localSensorHealthy ? String(initialLocal, 1) : "--";
    String initialNetworkStr = networkSensorHealthy ? String(initialNetwork, 1) : "--";
    String initialBlendedStr = (localSensorHealthy || networkSensorHealthy) ? String(initialBlended, 1) : "--";

    int currentHour = hour();
    int currentMinute = minute();
    int currentSecond = second();

    client.println("<!DOCTYPE html><html><head><title>S3 Dashboard</title>");
    client.println("<style>body{font-family:sans-serif; background:#f4f7f6; padding:20px; text-align:center;} .box{background:white; max-width:550px; margin:auto; padding:25px; border-radius:6px; box-shadow:0 2px 10px rgba(0,0,0,0.05); text-align:left;} input[type=text], input[type=number], select, textarea{width:100%; padding:10px; margin:5px 0 15px 0; border:1px solid #ccc; border-radius:4px; box-sizing:border-box;} .grid{display:flex; justify-content:space-between; margin-bottom:15px; flex-wrap:wrap;} .card{background:#edf1f5; padding:12px; border-radius:4px; width:30%; text-align:center; font-size:12px; box-sizing:border-box;} .card-large{width:100%; margin-bottom:15px; background:#edf1f5; padding:15px; border-radius:4px; text-align:center;} .fan-box{background:#f8f9fa; padding:10px; margin:5px 0; border-left:4px solid #17a2b8; display:flex; justify-content:space-between; font-size:14px;} .btn{width:100%; padding:14px; background:#28a745; color:white; border:none; font-weight:bold; font-size:16px; border-radius:4px; cursor:pointer; margin-top:20px;} .override-card{border:2px solid #dc3545; border-radius:6px; padding:15px; margin-bottom:15px; text-align:center;} .override-btn{width:100%; padding:12px; font-weight:bold; font-size:15px; border-radius:4px; border:2px solid #dc3545; background:#ffffff; color:#dc3545; cursor:pointer;} .override-btn.active-flash{animation:overrideFlash 1s step-start infinite;} @keyframes overrideFlash{0%,100%{background:#dc3545; color:#ffffff;} 50%{background:#330000; color:#dc3545;}} .override-slider-wrap{margin-top:12px; display:none;} .override-slider-wrap input[type=range]{width:100%;}</style>");

    client.println("<script>");
    client.print("let h = "); client.print(currentHour); client.println(";");
    client.print("let m = "); client.print(currentMinute); client.println(";");
    client.print("let s = "); client.print(currentSecond); client.println(";");
    client.print("const is24 = "); client.print(config.is24Hour ? "true" : "false"); client.println(";");

    client.println("function updateLiveClock() {");
    client.println("  s++; if(s>=60){ s=0; m++; if(m>=60){ m=0; h++; if(h>=24){ h=0; } } }");
    client.println("  let displayH = h; let ampm = '';");
    client.println("  if(!is24) { ampm = displayH >= 12 ? ' PM' : ' AM'; displayH = displayH % 12; if(displayH === 0) displayH = 12; }");
    client.println("  let strH = displayH < 10 ? '0'+displayH : displayH;");
    client.println("  let strM = m < 10 ? '0'+m : m;");
    client.println("  let strS = s < 10 ? '0'+s : s;");
    client.println("  let timeElement = document.getElementById('liveClockText');");
    client.println("  if(timeElement) { timeElement.innerText = strH + ':' + strM + ':' + strS + ampm; }");
    client.println("}");

    client.println("function fetchLiveTelemetry() {");
    client.println("  fetch(\"/ajax_data\").then(response => response.json()).then(data => {");
    client.println("    document.getElementById(\"liveLocalText\").innerText = data.local_temp;");
    client.println("    document.getElementById(\"liveNetText\").innerText = data.net_temp;");
    client.println("    document.getElementById(\"liveBlendText\").innerText = data.blend_temp;");
    client.println("    updateOverrideUI(data.override_active, data.override_speed);");
    client.println("    updateSdStatus(data.sd_present);");
    client.println("    data.fans.forEach((rpm, index) => {");
    client.println("      let fanEl = document.getElementById(\"fanRpm_\" + index);");
    client.println("      if(fanEl) fanEl.innerText = rpm + \" RPM\";");
    client.println("    });");
    client.println("  }).catch(err => console.error(\"Data drop:\", err));");
    client.println("}");

    // Manual override controls - mirrors the LCD touch UI / HA switch;
    // whichever surface changes it, the firmware's granular push functions
    // (pushOverrideSwitchToHA/pushOverrideSpeedToHA) keep the others in sync.
    client.println("let overrideDragging = false;");

    client.println("function updateOverrideUI(active, speed) {");
    client.println("  const btn = document.getElementById('overrideBtn');");
    client.println("  const wrap = document.getElementById('overrideSliderWrap');");
    client.println("  const slider = document.getElementById('overrideSlider');");
    client.println("  btn.dataset.active = active ? '1' : '0';");
    client.println("  btn.innerText = active ? 'Manual Override: ON (tap to disable)' : 'Manual Override (tap to enable)';");
    client.println("  btn.classList.toggle('active-flash', active);");
    client.println("  wrap.style.display = active ? 'block' : 'none';");
    client.println("  if (!overrideDragging) {");
    client.println("    slider.value = speed;");
    client.println("    document.getElementById('overrideSpeedLabel').innerText = Math.round(speed / 255 * 100) + '%';");
    client.println("  }");
    client.println("}");

    client.println("function updateSdStatus(present) {");
    client.println("  const el = document.getElementById('sdStatusText');");
    client.println("  if (!el) return;");
    client.println("  el.innerText = present ? 'OK' : 'Not present (buffering internally)';");
    client.println("  el.style.color = present ? '#28a745' : '#dc3545';");
    client.println("}");

    client.println("function toggleOverride() {");
    client.println("  const isActive = document.getElementById('overrideBtn').dataset.active === '1';");
    client.println("  const url = isActive ? '/override_set?active=0' : '/override_set?active=1&speed=255';");
    client.println("  fetch(url).then(() => fetchLiveTelemetry());");
    client.println("}");

    client.println("function onOverrideSliderInput(val) {");
    client.println("  overrideDragging = true;");
    client.println("  document.getElementById('overrideSpeedLabel').innerText = Math.round(val / 255 * 100) + '%';");
    client.println("}");

    client.println("function onOverrideSliderChange(val) {");
    client.println("  fetch('/override_set?speed=' + val).then(() => { overrideDragging = false; });");
    client.println("}");

    client.println("setInterval(updateLiveClock, 1000);");
    client.println("setInterval(fetchLiveTelemetry, 2000);");
    client.println("</script>");

    client.println("</head><body>");
    client.println("<div class='box'><h2 style='text-align:center; color:#0056b3; margin-top:0;'>ESP32-S3 Network Matrix Console</h2>");
    client.println("<div class='card-large'>&#128337; <strong>System Clock</strong><br><span id='liveClockText' style='font-size:20px; color:#0056b3; font-weight:bold;'>Syncing...</span></div>");

    // Manual override card - mirrors the LCD's Manual Control button + slider.
    // Rendered server-side with the true current state so a fresh page load
    // (before the first /ajax_data poll) shows the right thing immediately.
    {
        int initialPct = (manualOverrideDutyCycle * 100) / 255;
        client.println("<div class='override-card'>");
        client.print("<button type='button' id='overrideBtn' class='override-btn");
        client.print(manualOverrideActive ? " active-flash" : "");
        client.print("' onclick='toggleOverride()' data-active='");
        client.print(manualOverrideActive ? "1" : "0");
        client.print("'>");
        client.print(manualOverrideActive ? "Manual Override: ON (tap to disable)" : "Manual Override (tap to enable)");
        client.println("</button>");
        client.print("<div id='overrideSliderWrap' class='override-slider-wrap' style='display:");
        client.print(manualOverrideActive ? "block" : "none");
        client.println(";'>");
        client.print("Fan Speed: <span id='overrideSpeedLabel'>"); client.print(initialPct); client.println("%</span>");
        client.print("<input type='range' id='overrideSlider' min='0' max='255' value='"); client.print(manualOverrideDutyCycle);
        client.println("' oninput='onOverrideSliderInput(this.value)' onchange='onOverrideSliderChange(this.value)'>");
        client.println("</div>");
        client.println("</div>");
    }

    // SD card status - server-rendered with the true current state so a
    // fresh page load shows the right thing before the first poll.
    {
        bool sdOk = isSdCardPresent();
        client.print("<div class='card-large' style='text-align:center;'>&#128190; <strong>SD Card:</strong> ");
        client.print("<span id='sdStatusText' style='font-weight:bold; color:");
        client.print(sdOk ? "#28a745" : "#dc3545");
        client.print(";'>");
        client.print(sdOk ? "OK" : "Not present (buffering internally)");
        client.println("</span></div>");
    }

    client.println("<div class='grid'>");
    client.print("<div class='card'>&#128204; <strong>Local Probe</strong><br><span style='font-size:14px; color:#28a745; font-weight:bold;'><span id='liveLocalText'>"); client.print(initialLocalStr); client.println("</span> &deg;" + scaleSymbol + "</span></div>");
    client.print("<div class='card'>&#127760; <strong>HA Network</strong><br><span style='font-size:14px; color:#0056b3; font-weight:bold;'><span id='liveNetText'>"); client.print(initialNetworkStr); client.println("</span> &deg;" + scaleSymbol + "</span></div>");
    client.print("<div class='card'>&#9878; <strong>Blended Avg</strong><br><span style='font-size:14px; color:#e0a800; font-weight:bold;'><span id='liveBlendText'>"); client.print(initialBlendedStr); client.println("</span> &deg;" + scaleSymbol + "</span></div>");
    client.println("</div><hr>");

    client.println("<h3>Live Tachometer Metrics</h3>");
    for (int i = 0; i < 4; i++) {
        client.print("<div class='fan-box'><span><strong>Fan Channel " + String(i + 1) + "</strong> ");
        if (i < config.fanCount) client.print("<span style='color:#28a745; font-size:11px;'>[Active]</span>");
        else client.print("<span style='color:#dc3545; font-size:11px;'>[Disabled/Expansion]</span>");
        client.print("</span><span id='fanRpm_" + String(i) + "' style='font-weight:bold; color:#17a2b8;'>0 RPM</span></div>");
    }
    client.println("<hr>");

    client.println("<form action='/submit' method='post'>");
    client.println("<h3>Active Fan Channels</h3>");
    client.print("<div style='margin-bottom:20px; display:flex; gap:15px;'>");
    client.print("<label><input type='radio' name='fancnt' value='1' " + String(config.fanCount == 1 ? "checked" : "") + "> 1 Fan</label>");
    client.print("<label><input type='radio' name='fancnt' value='2' " + String(config.fanCount == 2 ? "checked" : "") + "> 2 Fans</label>");
    client.print("</div>");

    client.println("<h3>Thermal Profile Constraints</h3>");
    client.print("<div style='margin-bottom:15px;'>");
    client.print("<label style='margin-right:15px;'><input type='radio' name='unit' value='C' " + String(!config.isFahrenheit ? "checked" : "") + "> Celsius</label>");
    client.print("<label><input type='radio' name='unit' value='F' " + String(config.isFahrenheit ? "checked" : "") + "> Fahrenheit</label>");
    client.print("</div>");
    client.print("Min Activation Temp Limit: <input type='text' name='tmin' value='"); client.print(displayMin, 1); client.println("'>");
    client.print("Max Capacity Temp Limit: <input type='text' name='tmax' value='"); client.print(displayMax, 1); client.println("'>");

    client.println("<h3>Time Synchronization</h3>");
    client.print("Timezone Offset (Hours): <input type='number' name='tz' value='"); client.print(config.tzOffset); client.println("'>");
    client.print("<div style='margin-bottom:15px;'><label style='font-weight:bold; display:block; margin-bottom:5px;'>Format Mode:</label>");
    client.print("<label style='margin-right:15px;'><input type='radio' name='clk' value='12' " + String(!config.is24Hour ? "checked" : "") + "> 12-Hour</label>");
    client.print("<label><input type='radio' name='clk' value='24' " + String(config.is24Hour ? "checked" : "") + "> 24-Hour</label></div>");

    client.println("<h3>Static Network Configuration</h3>");
    client.print("Static IP Assignment: <input type='text' name='ip' value='"); client.print(config.ip.toString()); client.println("'>");
    client.print("Subnet Mask: <input type='text' name='sub' value='"); client.print(config.subnet.toString()); client.println("'>");
    client.print("Gateway: <input type='text' name='gw' value='"); client.print(config.gateway.toString()); client.println("'>");
    client.print("DNS Server: <input type='text' name='dns' value='"); client.print(config.dns.toString()); client.println("'>");

    client.println("<h3>Home Assistant Integration</h3>");
    client.print("Server IP / Host: <input type='text' name='hahost' value='"); client.print(config.haHost); client.println("' placeholder='e.g., 192.168.10.85' maxlength='63'>");
    client.print("Server API Port: <input type='text' name='haport' value='"); client.print(config.haPort); client.println("' placeholder='e.g., 8123' maxlength='10'>");
    client.print("Outbound Entity ID: <input type='text' name='nodeid' value='"); client.print(config.nodeID); client.println("' placeholder='e.g., fan_controller_01' maxlength='63'>");
    client.print("Inbound Sensor ID: <input type='text' name='hasensor' value='"); client.print(config.haSensor); client.println("' placeholder='e.g., rack_temperature' maxlength='63'>");
    client.print("Min Threshold input_number Entity: <input type='text' name='hatminentity' value='"); client.print(config.haTMinEntity); client.println("' placeholder='e.g., input_number.fan_ctrl_01_tmin' maxlength='63'>");
    client.print("Max Threshold input_number Entity: <input type='text' name='hatmaxentity' value='"); client.print(config.haTMaxEntity); client.println("' placeholder='e.g., input_number.fan_ctrl_01_tmax' maxlength='63'>");
    client.print("Long-Lived Bearer Token:<br><textarea name='hatoken' rows='4' maxlength='450'>"); client.print(config.haToken); client.println("</textarea>");

    client.flush();
    delay(5);

    client.println("<hr><div style='background:#edf1f5; padding:15px; border-radius:4px; margin:20px 0; font-size:12px; color:#555;'>");
    client.println("<label style='font-weight:bold; display:block; margin-bottom:5px; color:#333;'>Firmware Build Info</label>");
    client.print("<strong>Source File:</strong> "); client.print(SKETCH_FILENAME); client.println("<br>");
    // NOTE: __DATE__/__TIME__ are baked in whenever THIS file is actually
    // compiled, not whenever you upload - Arduino's incremental build
    // reuses this file's object file unchanged if this file itself hasn't
    // been edited, even while other files (and the overall binary) get
    // freshly rebuilt and flashed. If this timestamp looks stuck across
    // several uploads, that's why - it means only other files changed in
    // that stretch. This comment line is itself a trivial edit to force a
    // fresh recompile right now.
    client.print("<strong>Build Date:</strong> ");  client.print(__DATE__); client.println("<br>");
    client.print("<strong>Build Time:</strong> ");  client.print(__TIME__); client.println("");
    client.println("</div>");

    client.println("<input type='submit' class='btn' value='Apply Parameters &amp; Save'>");
    client.println("</form></div></body></html>");

    client.stop();
}
