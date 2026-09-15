/* ============================================================
   MQTT CLIENT — connects to HiveMQ Cloud over WebSocket
   ============================================================ */
const MQTT_CONFIG = {
    // WSS port (8884 for HiveMQ Cloud)
    url: 'wss://xxxxxxxxxxxx.s1.eu.hivemq.cloud:8884/mqtt',
    options: {
        username: 'your-username',
        password: 'your-password',
        clientId: 'nipas-dashboard-' + Math.random().toString(16).substr(2, 8),
        clean: true,
        reconnectPeriod: 3000,
        connectTimeout: 10000,
        keepalive: 30,
    },
    topics: {
        telemetry: 'nipas/esp32-01/telemetry',
        status:    'nipas/esp32-01/status',
        leaf:      'nipas/esp32-01/leaf',
        cmd:       'nipas/esp32-01/cmd',
        lwt:       'nipas/esp32-01/lwt',
    },
};

let mqttClient = null;
let mqttConnected = false;

function connectMQTT() {
    pushEvent('📡 Connecting to MQTT broker…', 'info');
    mqttClient = mqtt.connect(MQTT_CONFIG.url, MQTT_CONFIG.options);

    mqttClient.on('connect', () => {
        mqttConnected = true;
        pushEvent('✅ MQTT connected to broker', 'success');
        dom.connWs.className = 'conn-pill online';
        dom.connWs.innerHTML = '<span class="dot"></span> MQTT · live';

        // Subscribe to all robot topics
        Object.values(MQTT_CONFIG.topics).forEach(topic => {
            mqttClient.subscribe(topic, { qos: 1 }, (err) => {
                if (err) console.warn('Subscribe failed:', topic, err);
                else console.log('Subscribed:', topic);
            });
        });

        // Stop the local simulation once we're live
        clearInterval(streamTimer);
    });

    mqttClient.on('reconnect', () => {
        pushEvent('🔄 MQTT reconnecting…', 'warn');
        dom.connWs.className = 'conn-pill warn';
        dom.connWs.innerHTML = '<span class="dot"></span> MQTT · retry';
    });

    mqttClient.on('offline', () => {
        mqttConnected = false;
        dom.connWs.className = 'conn-pill offline';
        dom.connWs.innerHTML = '<span class="dot"></span> MQTT · offline';
    });

    mqttClient.on('error', (err) => {
        console.error('MQTT error:', err);
        pushEvent('❌ MQTT error: ' + err.message, 'alert');
    });

    mqttClient.on('message', (topic, payload) => {
        let data;
        try {
            data = JSON.parse(payload.toString());
        } catch (e) {
            console.warn('Bad MQTT payload:', payload.toString());
            return;
        }
        handleMQTTMessage(topic, data);
    });
}

/* ============================================================
   MQTT MESSAGE ROUTER
   ============================================================ */
function handleMQTTMessage(topic, data) {
    const T = MQTT_CONFIG.topics;

    /* --- TELEMETRY --- */
    if (topic === T.telemetry) {
        if (typeof data.moisture === 'number') {
            state.soilMoisture = data.moisture;
        }
        if (typeof data.temperature === 'number') {
            state.ambientTemp = data.temperature;
        }
        if (typeof data.humidity === 'number') {
            state.airHumidity = data.humidity;
        }

        // Feed the chart without regenerating values
        pushLivePoint(state.soilMoisture, state.ambientTemp, state.airHumidity);
        return;
    }

    /* --- STATUS (heartbeat + actuator state) --- */
    if (topic === T.status) {
        if (data.online !== undefined) {
            const el = document.getElementById('connSensors');
            if (data.online) {
                el.className = 'conn-pill online';
                el.innerHTML = '<span class="dot"></span> SENSORS';
            } else {
                el.className = 'conn-pill offline';
                el.innerHTML = '<span class="dot"></span> SENSORS · offline';
            }
        }
        if (typeof data.pump === 'boolean')  state.actuators.pump = data.pump;
        if (typeof data.valve === 'boolean') state.actuators.solenoid = data.valve;
        if (typeof data.probe === 'boolean') state.actuators.probe = data.probe;

        // Reflect hardware state in the UI
        syncActuatorUI();
        if (typeof data.buffer === 'number' && data.buffer > 0) {
            pushEvent(`📦 ${data.buffer} readings buffered offline`, 'info');
        }
        return;
    }

    /* --- LEAF SCAN RESULT --- */
    if (topic === T.leaf) {
        if (typeof data.infection === 'number') {
            state.leafInfection = data.infection;
            state.leafChlorosis = data.chlorosis || data.infection * 0.6;
            state.leafNecrosis  = data.necrosis  || data.infection * 0.4;
            state.leafSeverity  = data.severity  || 'none';
            state.leafHealthPct = clamp(100 - data.infection * 200, 0, 100);

            updateLeafUI();
            simulateLeafScan({ silent: true });   // redraw canvas only
            pushEvent(
                data.severity === 'high' ? `🦠 High infection detected (${(data.infection*100).toFixed(1)}%)`
              : data.severity === 'moderate' ? `⚠ Moderate infection (${(data.infection*100).toFixed(1)}%)`
              : data.severity === 'low' ? `🍃 Mild symptoms (${(data.infection*100).toFixed(1)}%)`
              : `✓ Leaf healthy`,
              data.severity === 'high' ? 'alert' : data.severity === 'moderate' ? 'warn' : 'success'
            );
        }
        return;
    }

    /* --- LAST WILL (robot went offline) --- */
    if (topic === T.lwt) {
        if (data.online === false) {
            pushEvent('⚠ Robot disconnected (LWT)', 'alert');
            dom.connSensors.className = 'conn-pill offline';
            dom.connSensors.innerHTML = '<span class="dot"></span> SENSORS · offline';
        }
    }
}

/* ============================================================
   PUSH LIVE DATA POINT INTO CHART
   (no simulation — values come from real MQTT messages)
   ============================================================ */
function pushLivePoint(moisture, temp, humidity) {
    const label = localTime();
    state.timeLabels.push(label);
    state.moistureHistory.push(clamp(moisture, CONFIG.MOISTURE_MIN, CONFIG.MOISTURE_MAX));
    state.tempHistory.push(clamp(temp, CONFIG.TEMP_MIN, CONFIG.TEMP_MAX));
    state.humidityHistory.push(clamp(humidity, CONFIG.HUMIDITY_MIN, CONFIG.HUMIDITY_MAX));

    while (state.timeLabels.length > CONFIG.MAX_POINTS) {
        state.timeLabels.shift();
        state.moistureHistory.shift();
        state.tempHistory.shift();
        state.humidityHistory.shift();
    }

    chart.data.labels = state.timeLabels.slice();
    chart.data.datasets[0].data = state.moistureHistory.slice();
    chart.data.datasets[1].data = state.tempHistory.slice();
    chart.update();

    state.packets++;
    updateKPIs();
    updateHardware();
}

/* ============================================================
   SEND COMMAND TO ROBOT VIA MQTT
   ============================================================ */
function sendMQTTCommand(cmd) {
    if (!mqttClient || !mqttConnected) {
        pushEvent('⚠ MQTT not connected — command dropped', 'warn');
        return false;
    }
    const payload = JSON.stringify({ cmd, ts: Date.now() });
    mqttClient.publish(MQTT_CONFIG.topics.cmd, payload, { qos: 1 });
    pushEvent(`📤 → ${cmd}`, 'info');
    return true;
}

/* ============================================================
   WIRE UP ACTUATOR BUTTONS TO MQTT (replaces the toggle handler)
   ============================================================ */
function attachMQTTActuator(btn, name, onCmd, offCmd) {
    btn.addEventListener('click', () => {
        const turningOn = !state.actuators[name];
        const cmd = turningOn ? onCmd : offCmd;
        if (sendMQTTCommand(cmd)) {
            // Optimistic UI update; real state comes from status topic
            state.actuators[name] = turningOn;
            syncActuatorUI();
        }
    });
}

attachMQTTActuator(dom.btnPump,     'pump',     'pump_on',     'pump_off');
attachMQTTActuator(dom.btnSolenoid, 'solenoid', 'valve_on',    'valve_off');
attachMQTTActuator(dom.btnProbe,    'probe',    'probe_deploy','probe_retract');
attachMQTTActuator(dom.btnVent,     'vent',     'vent_on',     'vent_off');   // add if you wire a fan

dom.btnFungicide.addEventListener('click', () => {
    sendMQTTCommand('fungicide_dose');
    pushEvent('🧪 Fungicide command sent', 'success');
});

dom.btnScan.addEventListener('click', () => {
    sendMQTTCommand('scan');
    pushEvent('📷 Leaf scan requested', 'info');
});

/* ============================================================
   SYNC ACTUATOR BUTTON STATE FROM ROBOT
   ============================================================ */
function syncActuatorUI() {
    updateActuatorButton(dom.btnPump,     state.actuators.pump,     'RUNNING','OFF');
    updateActuatorButton(dom.btnSolenoid, state.actuators.solenoid, 'OPEN','CLOSED');
    updateActuatorButton(dom.btnProbe,    state.actuators.probe,    'DEPLOYED','RETRACTED');
    updateActuatorButton(dom.btnVent,     state.actuators.vent,     'ON','OFF');
    updateHardware();
}

/* ============================================================
   BOOT
   ============================================================ */
connectMQTT();