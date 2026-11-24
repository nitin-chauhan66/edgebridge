/* EdgeBridge-S3 Phase 0 Interactive Documentation App */

const FLASH_PARTITIONS = [
  { name: "boot_partition", label: "mcuboot", offset: "0x000000", size: "64 KB", pct: 4, color: "#6366f1", desc: "MCUboot bootloader with ECDSA-P256 image signature verification." },
  { name: "slot0_partition", label: "slot0 (image-0)", offset: "0x010000", size: "4 MB", pct: 25, color: "#0284c7", desc: "Primary active application slot running Zephyr RTOS." },
  { name: "slot1_partition", label: "slot1 (image-1)", offset: "0x410000", size: "4 MB", pct: 25, color: "#0ea5e9", desc: "Secondary OTA staging slot for resumable chunked downloads." },
  { name: "scratch_partition", label: "scratch", offset: "0x810000", size: "256 KB", pct: 6, color: "#8b5cf6", desc: "MCUboot swap scratch metadata for power-cut-safe upgrades." },
  { name: "nvs_config_partition", label: "nvs_config", offset: "0x850000", size: "512 KB", pct: 8, color: "#f59e0b", desc: "Dual-bank active + backup configuration with boot rollback flag." },
  { name: "telemetry_fs_partition", label: "telemetry_fs (LittleFS)", offset: "0x8D0000", size: "7.1 MB", pct: 32, color: "#10b981", desc: "Wear-leveled LittleFS circular store-and-forward ring with dual superblocks." }
];

const BUILTIN_PROFILES = {
  jk_bms_modbus_v1: {
    profile_id: "jk_bms_modbus_v1",
    device_name: "JK-PB BMS (RS-485 Modbus RTU)",
    bus: "rs485",
    default_baud: 115200,
    slave_id: 1,
    poll_interval_ms: 1000,
    turnaround_guard_us: 2000,
    registers: [
      { name: "pack_voltage_v", func: 3, address: 4608, length: 1, scale: 0.01, unit: "V" },
      { name: "pack_current_a", func: 3, address: 4609, length: 2, signed: true, scale: 0.001, unit: "A" },
      { name: "soc_percent", func: 3, address: 4611, length: 1, scale: 1.0, unit: "%" }
    ]
  },
  pylontech_can_v1: {
    profile_id: "pylontech_can_v1",
    device_name: "Pylontech/Victron CAN BMS (500 kbps)",
    bus: "can",
    default_baud: 500000,
    slave_id: 1,
    poll_interval_ms: 1000,
    registers: [
      { name: "charge_voltage_limit_v", can_id: 849, byte_offset: 0, length: 2, little_endian: true, scale: 0.1, unit: "V" },
      { name: "soc_percent", can_id: 853, byte_offset: 0, length: 2, little_endian: true, scale: 1.0, unit: "%" },
      { name: "soh_percent", can_id: 853, byte_offset: 2, length: 2, little_endian: true, scale: 1.0, unit: "%" },
      { name: "pack_voltage_v", can_id: 854, byte_offset: 0, length: 2, little_endian: true, scale: 0.01, unit: "V" },
      { name: "pack_current_a", can_id: 854, byte_offset: 2, length: 2, signed: true, little_endian: true, scale: 0.1, unit: "A" },
      { name: "pack_temp_c", can_id: 854, byte_offset: 4, length: 2, signed: true, little_endian: true, scale: 0.1, unit: "C" }
    ]
  },
  deye_inverter_modbus_v1: {
    profile_id: "deye_inverter_modbus_v1",
    device_name: "Deye/Sunsynk Hybrid Inverter (RS-485)",
    bus: "rs485",
    default_baud: 9600,
    slave_id: 1,
    poll_interval_ms: 1000,
    turnaround_guard_us: 500,
    registers: [
      { name: "battery_soc_pct", func: 3, address: 588, length: 1, scale: 1.0, unit: "%" },
      { name: "battery_power_w", func: 3, address: 590, length: 1, signed: true, scale: 1.0, unit: "W" },
      { name: "grid_power_w", func: 3, address: 625, length: 1, signed: true, scale: 1.0, unit: "W" },
      { name: "pv1_power_w", func: 3, address: 672, length: 1, scale: 1.0, unit: "W" }
    ]
  },
  growatt_inverter_modbus_v1: {
    profile_id: "growatt_inverter_modbus_v1",
    device_name: "Growatt Inverter (Modbus 0x04)",
    bus: "rs485",
    default_baud: 9600,
    slave_id: 2,
    poll_interval_ms: 1000,
    turnaround_guard_us: 500,
    registers: [
      { name: "pv_input_power_w", func: 4, address: 1, length: 2, scale: 0.1, unit: "W" },
      { name: "grid_voltage_v", func: 4, address: 38, length: 1, scale: 0.1, unit: "V" },
      { name: "ac_output_power_w", func: 4, address: 35, length: 2, scale: 0.1, unit: "W" }
    ]
  }
};

const SEARCH_INDEX = [
  { title: "Overview & Hardware Quirks Solved", route: "overview", tags: "phase 0 prd waveshare quirks ble sram usb" },
  { title: "Waveshare Pinout (GPIO17, GPIO18, GPIO15, GPIO16)", route: "hardware", tags: "pinout rs485 can twai usb cdc gpio21 120r antenna" },
  { title: "16 MB Flash Partition Layout (MCUboot + LittleFS)", route: "hardware", tags: "flash slot0 slot1 scratch nvs_config telemetry_fs" },
  { title: "3-Thread RTOS Architecture & PSRAM Offload", route: "architecture", tags: "threads poller storage mqtt web analyzer sram psram" },
  { title: "Dual-Superblock Storage & 60s Config Watchdog", route: "architecture", tags: "power cut littlefs crc32 nvs rollback watchdog" },
  { title: "Interactive Bus Analyzer & t3.5 Calculator", route: "simulator", tags: "calculator baud turnaround simulator diagnostics can ack" },
  { title: "Device Profiles (JK BMS, Pylontech CAN, Deye, Growatt)", route: "profiles", tags: "json profile modbus can registers decoder" },
  { title: "REST API & MQTT Telemetry Reference", route: "api", tags: "/api/v1/diagnostics /api/v1/scan /api/v1/config mqtt" },
  { title: "Build, Flash & USB-C Bootloader Recovery", route: "deployment", tags: "west build flash boot reset cdc acm" },
  { title: "Verification Suite (500-Cut Torture & 7-Day Sim)", route: "verification", tags: "tests asan ubsan torture simulation 3024000" }
];

/* Simulator & Checklist State */
const simState = {
  baud: 115200,
  guardExtraUs: 250,
  autoDir: true,
  strictCap: true,
  canFault: "healthy",
  watchdogSec: 0,
  watchdogTimer: null,
  scanDone: false,
  activeProfile: "jk_bms_modbus_v1",
  checklist: [false, false, false, false, false]
};

function escHtml(s) {
  return String(s).replace(/[&<>"]/g, (ch) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[ch]));
}

function codeBlock(title, codeText) {
  const id = "cb_" + Math.random().toString(36).slice(2, 9);
  return `
    <div class="code-block">
      <div class="code-head">
        <span>${escHtml(title)}</span>
        <button class="copy-btn" onclick="copyById('${id}')">Copy</button>
      </div>
      <pre id="${id}">${escHtml(codeText)}</pre>
    </div>`;
}

window.copyById = function (id) {
  const el = document.getElementById(id);
  if (!el) return;
  navigator.clipboard.writeText(el.textContent).then(() => showToast("Copied to clipboard"));
};

function showToast(msg) {
  const t = document.getElementById("toast");
  t.textContent = msg;
  t.classList.add("show");
  setTimeout(() => t.classList.remove("show"), 1800);
}

/* --- View Renderers --- */

function renderOverview() {
  const doneCount = simState.checklist.filter(Boolean).length;
  return `
    <div class="view-header">
      <div class="eyebrow">Phase 0 • Zero-Capex Firmware for Waveshare ESP32-S3-RS485-CAN</div>
      <h1>EdgeBridge-S3 Documentation App</h1>
      <p>Self-healing RS-485 &amp; CAN edge gateway with built-in Web Bus Analyzer, built on Zephyr RTOS, MCUboot, and power-cut-safe LittleFS for the 16 MB Flash + 8 MB Octal PSRAM ESP32-S3R8.</p>
    </div>

    <div class="grid-4">
      <div class="stat-card">
        <div class="stat-label">Target Hardware</div>
        <div class="stat-value">Waveshare S3</div>
        <div class="stat-sub">₹2,049 off-the-shelf DIN unit</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">Power-Cut Torture</div>
        <div class="stat-value">500 / 500</div>
        <div class="stat-sub">0 corruptions, 0 lost records</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">7-Day High-Rate Sim</div>
        <div class="stat-value">3,024,000</div>
        <div class="stat-sub">4 Modbus + 1 CAN, 0 dropped</div>
      </div>
      <div class="stat-card">
        <div class="stat-label">Internal SRAM Budget</div>
        <div class="stat-value">&lt; 35%</div>
        <div class="stat-sub">Buffers offloaded to 8MB PSRAM</div>
      </div>
    </div>

    <h2 class="section-title">5 Real-World Hardware Quirks Solved in Phase 0</h2>
    <div class="grid-2">
      <div class="card">
        <h3>1. Auto-Direction RS-485 Clipping &amp; Baud Limits</h3>
        <p>Waveshare uses hardware auto-direction (no DE/RE pin required) and caps reliably around <code>115,200 bps</code> due to isolator propagation delay. <code>eb_rs485.c</code> enforces <code>t3.5</code> inter-frame guards, flushes stale RX noise, warns/clamps above 115.2 kbps, and auto-escalates turnaround margin on clipped replies.</p>
      </div>
      <div class="card">
        <h3>2. BLE + Wi-Fi SRAM Crashes</h3>
        <p>Running BLE alongside Wi-Fi TLS and bus drivers exhausts internal SRAM. <code>eb_mem.h</code> routes ring buffers, JSON scratch, and profile tables to 8 MB Octal PSRAM (<code>.ext_ram.bss</code>), while <code>eb_ble_prov.c</code> calls <code>bt_disable()</code> immediately after provisioning or timeout.</p>
      </div>
      <div class="card">
        <h3>3. CAN LED Ambiguity &amp; Silent Bus-Off</h3>
        <p>Because CAN transceivers echo their own TX frames, TX/RX LEDs blink even when no peer ACKs. <code>eb_can.c</code> surfaces real <code>TEC</code>/<code>REC</code>, <code>ack_ok</code> vs <code>ack_missing</code>, <code>ERROR_PASSIVE</code>/<code>BUS_OFF</code> states, and exponential recovery backoff in the Web Bus Analyzer.</p>
      </div>
      <div class="card">
        <h3>4. USB-C CDC Bootloader &amp; DTR Log Trap</h3>
        <p>Native USB-C CDC consoles can stall boot logs if DTR is not asserted. <code>prj.conf</code> enables non-blocking deferred logging (<code>CONFIG_LOG_MODE_DEFERRED=y</code>, <code>CONFIG_UART_LINE_CTRL=n</code>) and documents deterministic <code>BOOT + RESET</code> recovery.</p>
      </div>
    </div>

    <h2 class="section-title">Interactive 5-Minute Field Commissioning Checklist (${doneCount}/5)</h2>
    <div class="card">
      ${[
        "Open DIN case only if needed to set RS485/CAN 120Ω jumper caps (120R at bus endpoints, NC mid-bus).",
        "Wire RS-485 A+/B- and/or CANH/CANL + 9–36V DC input and power on.",
        "Connect phone to EdgeBridge-S3-AP or BLE provisioning and open http://192.168.4.1/.",
        "Run POST /api/v1/scan to auto-detect RS-485 baud/slave IDs and CAN bitrate, then load BMS/Inverter JSON profile.",
        "Stage Wi-Fi & MQTT credentials and click Confirm Config within the 60-second watchdog window."
      ].map((step, i) => `
        <div class="checklist-item ${simState.checklist[i] ? "done" : ""}" onclick="toggleCheck(${i})">
          <input type="checkbox" ${simState.checklist[i] ? "checked" : ""} aria-label="Step ${i + 1}">
          <div><strong>Step ${i + 1}:</strong> ${escHtml(step)}</div>
        </div>
      `).join("")}
    </div>
  `;
}

window.toggleCheck = function (idx) {
  simState.checklist[idx] = !simState.checklist[idx];
  renderRoute("overview");
};

function renderHardware() {
  return `
    <div class="view-header">
      <div class="eyebrow">Hardware &amp; DeviceTree • boards/waveshare_esp32s3_rs485_can.overlay</div>
      <h1>Waveshare Pinout &amp; 16 MB Flash Layout</h1>
      <p>Verified GPIO assignments and fixed-partition flash map for the Waveshare ESP32-S3-RS485-CAN (ESP32-S3R8: 16 MB Flash + 8 MB Octal PSRAM).</p>
    </div>

    <h2 class="section-title">ESP32-S3 GPIO Signal Mapping</h2>
    <div class="table-wrap">
      <table>
        <thead><tr><th>Interface</th><th>Signal</th><th>ESP32-S3 GPIO</th><th>Phase 0 Firmware Behavior</th></tr></thead>
        <tbody>
          <tr><td><strong>RS-485</strong></td><td>TX</td><td><code>GPIO17</code></td><td>UART1 TX via isolated half-duplex transceiver</td></tr>
          <tr><td><strong>RS-485</strong></td><td>RX</td><td><code>GPIO18</code></td><td>UART1 RX with internal pull-up &amp; ISR ring queue</td></tr>
          <tr><td><strong>RS-485</strong></td><td>DE / EN (Optional)</td><td><code>GPIO21</code></td><td>Unused in hardware auto-direction default; toggled if <code>rs485_auto_dir=false</code></td></tr>
          <tr><td><strong>CAN (TWAI)</strong></td><td>TX</td><td><code>GPIO15</code></td><td>Isolated CAN 2.0B controller TX</td></tr>
          <tr><td><strong>CAN (TWAI)</strong></td><td>RX</td><td><code>GPIO16</code></td><td>Isolated CAN 2.0B controller RX with accept-all filter</td></tr>
          <tr><td><strong>USB-C</strong></td><td>D- / D+</td><td><code>GPIO19</code> / <code>GPIO20</code></td><td>Native USB CDC-ACM console (deferred non-blocking logs)</td></tr>
        </tbody>
      </table>
    </div>

    <div class="callout warn">
      <strong>Phase 0 Hardware Reminders:</strong>
      <ul style="margin:6px 0 0;padding-left:18px">
        <li><strong>120 Ω Termination:</strong> Set physical <code>RS485 120R</code> / <code>CAN 120R</code> jumper caps inside the Waveshare enclosure to <code>120R</code> at bus endpoints or <code>NC</code> mid-bus.</li>
        <li><strong>External Antenna:</strong> Switching from onboard ceramic to IPEX1 on the Waveshare board requires rotating the 0402/0603 RF resistor.</li>
      </ul>
    </div>

    <h2 class="section-title">Interactive 16 MB Flash Partition Map</h2>
    <div class="card">
      <p>Click any partition segment to inspect exact offsets and responsibilities:</p>
      <div class="flash-bar">
        ${FLASH_PARTITIONS.map((p, idx) => `
          <div class="flash-seg" style="width:${p.pct}%;background:${p.color}" onclick="selectPartition(${idx})" title="${p.label} (${p.size})">
            ${p.label}
          </div>
        `).join("")}
      </div>
      <div id="partInspector" class="callout ok">
        <strong>${FLASH_PARTITIONS[5].name}</strong> (<code>${FLASH_PARTITIONS[5].offset}</code>, ${FLASH_PARTITIONS[5].size}): ${FLASH_PARTITIONS[5].desc}
      </div>
      <div class="table-wrap" style="margin-top:12px">
        <table>
          <thead><tr><th>Offset</th><th>Size</th><th>Node / Label</th><th>Purpose</th></tr></thead>
          <tbody>
            ${FLASH_PARTITIONS.map((p) => `
              <tr><td><code>${p.offset}</code></td><td>${p.size}</td><td><code>${p.name}</code></td><td>${escHtml(p.desc)}</td></tr>
            `).join("")}
          </tbody>
        </table>
      </div>
    </div>
  `;
}

window.selectPartition = function (idx) {
  const p = FLASH_PARTITIONS[idx];
  const el = document.getElementById("partInspector");
  if (el && p) {
    el.innerHTML = `<strong>${escHtml(p.name)}</strong> (<code>${p.offset}</code>, ${p.size}): ${escHtml(p.desc)}`;
  }
};

function renderArchitecture() {
  return `
    <div class="view-header">
      <div class="eyebrow">Deterministic Runtime • src/main.c</div>
      <h1>3-Thread Architecture &amp; Self-Healing Design</h1>
      <p>Bus polling never blocks on Wi-Fi, TLS, flash writes, or HTTP requests. All components communicate via bounded lock-free queues and CRC-protected storage banks.</p>
    </div>

    <div class="pipeline">
      <div class="card">
        <span class="badge badge-info">Thread 1 • Priority 2</span>
        <h3 style="margin-top:8px">Bus Poller</h3>
        <p>Executes multi-slave Modbus RTU schedules with adaptive <code>t3.5</code> guards, drains TWAI CAN frames, tracks <code>TEC/REC</code> &amp; Bus-Off recovery, and pushes records into lock-free SPSC <code>eb_ring_queue</code>.</p>
      </div>
      <div class="pipe-arrow">&rarr;</div>
      <div class="card">
        <span class="badge badge-ok">Thread 2 • Priority 5</span>
        <h3 style="margin-top:8px">Storage &amp; MQTT Batcher</h3>
        <p>Drains SPSC queue into dual-superblock LittleFS circular storage (<code>eb_storage.c</code>) and publishes oldest-first FIFO JSON batches via MQTT, acknowledging slots only after publish confirmation.</p>
      </div>
      <div class="pipe-arrow">&rarr;</div>
      <div class="card">
        <span class="badge badge-warn">Thread 3 • Priority 8</span>
        <h3 style="margin-top:8px">Web UI, Watchdog &amp; OTA</h3>
        <p>Serves HTTP Bus Analyzer (:80), runs 60s NVS config rollback watchdog, disables BLE after provisioning, gates MCUboot confirmation on sustained bus + network health, and feeds ESP32 WDT.</p>
      </div>
    </div>

    <h2 class="section-title">Memory Budget: PSRAM Offload (&lt; 35% Internal SRAM Target)</h2>
    <div class="grid-2">
      <div class="card">
        <h3>Internal SRAM (512 KB)</h3>
        <div class="meter"><div class="meter-fill" style="width:24%;background:#34d399"></div></div>
        <p><strong>~24% used</strong> — RTOS kernel stacks, UART/CAN ISRs, and control structs only. Zero large telemetry buffers in internal DRAM.</p>
      </div>
      <div class="card">
        <h3>Octal PSRAM (8 MB • <code>.ext_ram.bss</code>)</h3>
        <div class="meter"><div class="meter-fill" style="width:18%;background:#38bdf8"></div></div>
        <p><strong>8 MB available</strong> — Holds <code>g_queue_buf</code>, <code>g_profiles</code>, <code>g_http_resp_buf</code>, <code>g_mqtt_json_buf</code>, and mbedTLS heap.</p>
      </div>
    </div>

    <h2 class="section-title">Power-Cut &amp; Remote Lockout Guarantees</h2>
    <div class="grid-2">
      <div class="card">
        <h3>Dual-Superblock LittleFS Ring (<code>eb_storage.c</code>)</h3>
        <p>Alternates checkpoints between two superblock banks (<code>generation &amp; 1</code>) with IEEE CRC-32. On boot, picks the highest valid generation, replays any valid-CRC slots appended after the last checkpoint, and skips any half-written corrupt slot cleanly.</p>
      </div>
      <div class="card">
        <h3>60s Watchdog + Crash-Reboot Rollback (<code>eb_config.c</code>)</h3>
        <p>Staging a config writes <code>active</code>, <code>backup</code>, and <code>pending_confirm=true</code> to NVS. If unconfirmed for 60s—or if the device reboots/crashes before confirmation—EdgeBridge automatically reverts to <code>backup</code>.</p>
      </div>
    </div>
  `;
}

function calcT35Us(baud, extraUs) {
  let t35 = 1750;
  if (baud <= 19200) {
    t35 = Math.max(1750, Math.ceil(38500000 / baud));
  }
  return t35 + extraUs;
}

function renderSimulator() {
  const effectiveBaud = (simState.baud > 115200 && simState.strictCap) ? 115200 : simState.baud;
  const ceilingWarn = simState.baud > 115200;
  const t35 = calcT35Us(effectiveBaud, simState.guardExtraUs);
  const frameTxUs = Math.ceil((8 * 11 * 1000000) / effectiveBaud);

  const canBadge = simState.canFault === "healthy"
    ? `<span class="badge badge-ok">ERROR_ACTIVE • Peer ACK OK</span>`
    : simState.canFault === "no_ack"
      ? `<span class="badge badge-warn">ERROR_PASSIVE • No Peer ACK</span>`
      : `<span class="badge badge-err">BUS_OFF • Auto-Recovery Backoff</span>`;

  const canHint = simState.canFault === "healthy"
    ? "Healthy: Peer ACKing and active CAN traffic verified"
    : simState.canFault === "no_ack"
      ? "No peer ACK (Error Passive): Check inverter/BMS power, CANH/CANL polarity, and 120R jumper"
      : "BUS_OFF: Check shorted CANH/CANL, 120R termination, or bitrate mismatch";

  return `
    <div class="view-header">
      <div class="eyebrow">Interactive Lab • eb_rs485.c &amp; eb_web_analyzer.c</div>
      <h1>Bus Analyzer Sandbox &amp; t3.5 Calculator</h1>
      <p>Experiment with Waveshare RS-485 isolator timing limits and preview the embedded mobile Web Bus Analyzer diagnostics workflow.</p>
    </div>

    <div class="grid-2">
      <div class="card">
        <h3>RS-485 t3.5 &amp; Isolator Timing Calculator</h3>
        <div class="form-row" style="margin:12px 0">
          <label>Baud:
            <select id="simBaud" onchange="updateBaudSim()">
              ${[9600, 19200, 38400, 57600, 115200, 230400].map((b) =>
                `<option value="${b}" ${simState.baud === b ? "selected" : ""}>${b.toLocaleString()} bps</option>`
              ).join("")}
            </select>
          </label>
          <label>Guard Margin (µs):
            <input class="input" id="simMargin" type="number" min="0" max="5000" step="50" value="${simState.guardExtraUs}" style="width:92px" onchange="updateBaudSim()">
          </label>
          <label style="display:flex;align-items:center;gap:6px">
            <input type="checkbox" id="simStrict" ${simState.strictCap ? "checked" : ""} onchange="updateBaudSim()"> Strict 115.2k Cap
          </label>
        </div>
        <div class="callout ${ceilingWarn ? "warn" : "ok"}">
          <strong>Effective Baud:</strong> <code>${effectiveBaud.toLocaleString()} bps</code>
          ${ceilingWarn ? (simState.strictCap ? "(Clamped from " + simState.baud.toLocaleString() + " — Waveshare isolator ceiling)" : "(Warning: exceeds 115,200 bps isolator limit)") : "(Within verified isolator limit)"}<br>
          <strong>t3.5 Inter-Frame Guard:</strong> <code>${t35} µs</code> | <strong>8-byte TX Time:</strong> <code>${frameTxUs} µs</code>
        </div>
      </div>

      <div class="card">
        <h3>Live Web Bus Analyzer Preview</h3>
        <div class="form-row" style="margin:10px 0">
          <label>Simulate CAN State:
            <select id="simCanSel" onchange="updateCanSim(this.value)">
              <option value="healthy" ${simState.canFault === "healthy" ? "selected" : ""}>Healthy (Peer ACK)</option>
              <option value="no_ack" ${simState.canFault === "no_ack" ? "selected" : ""}>Missing Peer ACK</option>
              <option value="bus_off" ${simState.canFault === "bus_off" ? "selected" : ""}>Bus-Off Fault</option>
            </select>
          </label>
          <button class="btn" onclick="simRunScan()">Run Auto-Scan</button>
          <button class="btn btn-warn" onclick="simStageCfg()">Stage Config (60s)</button>
          ${simState.watchdogSec > 0 ? `<button class="btn" style="background:#059669" onclick="simConfirmCfg()">Confirm (${simState.watchdogSec}s)</button>` : ""}
        </div>
        <div style="margin-top:10px">
          ${canBadge}
          <p style="margin-top:8px">${escHtml(canHint)}</p>
          ${simState.scanDone ? `<p style="margin-top:8px;color:var(--green)"><strong>Scan Hit:</strong> RS-485 Slave #1 @ 115,200 bps (JK-PB BMS) + CAN @ 500 kbps (IDs 0x351, 0x355, 0x356)</p>` : ""}
        </div>
      </div>
    </div>
  `;
}

window.updateBaudSim = function () {
  simState.baud = Number(document.getElementById("simBaud").value);
  simState.guardExtraUs = Math.max(0, Number(document.getElementById("simMargin").value) || 0);
  simState.strictCap = document.getElementById("simStrict").checked;
  renderRoute("simulator");
};

window.updateCanSim = function (val) {
  simState.canFault = val;
  renderRoute("simulator");
};

window.simRunScan = function () {
  simState.scanDone = true;
  showToast("Auto-scan complete: 1 RS-485 slave & CAN 500k detected");
  renderRoute("simulator");
};

window.simStageCfg = function () {
  simState.watchdogSec = 60;
  if (simState.watchdogTimer) clearInterval(simState.watchdogTimer);
  simState.watchdogTimer = setInterval(() => {
    if (simState.watchdogSec > 1) {
      simState.watchdogSec--;
      if (currentRoute === "simulator") renderRoute("simulator");
    } else {
      clearInterval(simState.watchdogTimer);
      simState.watchdogSec = 0;
      showToast("60s Watchdog expired: auto-reverted to backup config");
      if (currentRoute === "simulator") renderRoute("simulator");
    }
  }, 1000);
  renderRoute("simulator");
};

window.simConfirmCfg = function () {
  if (simState.watchdogTimer) clearInterval(simState.watchdogTimer);
  simState.watchdogSec = 0;
  showToast("Configuration confirmed and persisted to backup bank");
  renderRoute("simulator");
};

function renderProfiles() {
  const keys = Object.keys(BUILTIN_PROFILES);
  const p = BUILTIN_PROFILES[simState.activeProfile] || BUILTIN_PROFILES.jk_bms_modbus_v1;
  return `
    <div class="view-header">
      <div class="eyebrow">Profile Engine • src/eb_profile.c &amp; profiles/</div>
      <h1>Built-In Inverter &amp; BMS JSON Profiles</h1>
      <p>Zero-allocation JSON parser supports up to 5 active profiles concurrently (e.g., 4 Modbus slaves + 1 CAN BMS at 1-second polling intervals).</p>
    </div>

    <div class="tabs">
      ${keys.map((k) => `
        <button class="tab-btn ${simState.activeProfile === k ? "active" : ""}" onclick="selectProfile('${k}')">${k}</button>
      `).join("")}
    </div>

    <div class="grid-2">
      <div class="card">
        <h3>${escHtml(p.device_name)}</h3>
        <p style="margin-bottom:12px">Bus: <code>${p.bus.toUpperCase()}</code> • Rate: <code>${p.default_baud.toLocaleString()} bps</code> • Slave: <code>${p.slave_id}</code></p>
        <div class="table-wrap">
          <table>
            <thead><tr><th>Signal</th><th>${p.bus === "can" ? "CAN ID / Offset" : "FC / Reg"}</th><th>Len</th><th>Scale</th><th>Unit</th></tr></thead>
            <tbody>
              ${p.registers.map((r) => `
                <tr>
                  <td><code>${escHtml(r.name)}</code></td>
                  <td><code>${p.bus === "can" ? "0x" + r.can_id.toString(16).toUpperCase() + " +" + r.byte_offset : "FC" + r.func + " @" + r.address}</code></td>
                  <td>${r.length}</td>
                  <td>${r.scale}</td>
                  <td>${escHtml(r.unit)}</td>
                </tr>
              `).join("")}
            </tbody>
          </table>
        </div>
      </div>
      <div>
        ${codeBlock(p.profile_id + ".json", JSON.stringify(p, null, 2))}
      </div>
    </div>
  `;
}

window.selectProfile = function (k) {
  simState.activeProfile = k;
  renderRoute("profiles");
};

function renderApi() {
  const diagSample = `{
  "rs485": { "baud": 115200, "t35_us": 2000, "adaptive_us": 0, "auto_dir": true, "tx": 120, "rx": 120, "crc_err": 0, "clipped_err": 0 },
  "can": { "bitrate": 500000, "state": "ERROR_ACTIVE", "tec": 0, "rec": 0, "ack_ok": 45, "ack_miss": 0, "bus_off": 0, "peer_ack": true },
  "storage": { "buffered": 0, "capacity": 16384, "overwritten": 0, "corrupt_skipped": 0 },
  "config": { "version": 3, "pending_confirm": false, "rollbacks": 0, "profile": "jk_bms_modbus_v1" },
  "watchdog_remaining_sec": 0,
  "memory": { "internal_sram_pct": 24, "psram_active": true }
}`;

  const mqttSample = `{
  "device_id": "edgebridge-s3",
  "seq": 1042,
  "ts_ms": 1842000,
  "bus": "rs485",
  "addr": 1,
  "can_id": 0,
  "raw_hex": "010302149C72A1",
  "metrics": { "pack_voltage_v": 52.760, "soc_percent": 88.000 }
}`;

  return `
    <div class="view-header">
      <div class="eyebrow">Web Server (:80) &amp; MQTT • eb_web_analyzer.c &amp; eb_mqtt.c</div>
      <h1>REST API &amp; MQTT Telemetry Reference</h1>
      <p>Portable HTTP/1.1 API used both by the mobile Web Bus Analyzer and automated commissioning scripts.</p>
    </div>

    <div class="table-wrap">
      <table>
        <thead><tr><th>Method &amp; Path</th><th>Purpose</th></tr></thead>
        <tbody>
          <tr><td><code>GET /</code></td><td>Mobile-first Bus Analyzer &amp; 5-minute commissioning SPA</td></tr>
          <tr><td><code>GET /api/v1/diagnostics</code></td><td>Full RS-485, CAN, LittleFS, Watchdog, BLE, MQTT, OTA &amp; SRAM/PSRAM JSON</td></tr>
          <tr><td><code>GET /api/v1/telemetry</code></td><td>Latest decoded metrics + recent 16 RS-485 &amp; CAN sniffer frames</td></tr>
          <tr><td><code>GET|POST /api/v1/scan</code></td><td>Trigger or read RS-485 baud/slave-ID &amp; listen-only CAN bitrate scan</td></tr>
          <tr><td><code>GET|POST /api/v1/profiles</code></td><td>List built-in/active profiles or load <code>{"builtin":"..."}</code> / custom JSON</td></tr>
          <tr><td><code>GET|POST /api/v1/config</code></td><td>Read active config or stage changes (arms 60s rollback watchdog)</td></tr>
          <tr><td><code>POST /api/v1/config/confirm</code></td><td>Confirm staged config and commit to NVS backup bank</td></tr>
          <tr><td><code>GET /api/v1/ota</code></td><td>Inspect MCUboot slot confirmation and chunked download progress</td></tr>
        </tbody>
      </table>
    </div>

    <div class="grid-2" style="margin-top:16px">
      <div>${codeBlock("GET /api/v1/diagnostics (Sample Response)", diagSample)}</div>
      <div>${codeBlock("MQTT Topic: edgebridge/<device_id>/telemetry", mqttSample)}</div>
    </div>
  `;
}

function renderDeployment() {
  return `
    <div class="view-header">
      <div class="eyebrow">Build, Flash &amp; Field Recovery • sysbuild.conf</div>
      <h1>Building, Flashing &amp; USB-C Boot Trap</h1>
      <p>Build the Zephyr RTOS + MCUboot ECDSA-P256 firmware for the Waveshare ESP32-S3-RS485-CAN or run host verification tests.</p>
    </div>

    <h2 class="section-title">1. Build &amp; Flash Zephyr Firmware</h2>
    ${codeBlock("Zephyr Sysbuild (MCUboot + Overlay)", `west build -b esp32s3_devkitm/esp32s3/procpu --sysbuild . -- \\\n  -DDTC_OVERLAY_FILE=boards/waveshare_esp32s3_rs485_can.overlay\nwest flash`)}

    <h2 class="section-title">2. Host C Verification Suite (GCC + ASan + UBSan)</h2>
    ${codeBlock("Host Test Runner", `cd tests\nchmod +x run_tests.sh\n./run_tests.sh`)}

    <h2 class="section-title">3. Fixing the Native USB-C Bootloader Trap</h2>
    <div class="callout warn">
      If flashing over native USB-C stalls at <code>waiting for download...</code>:<br>
      1. Hold the <strong>BOOT</strong> button on the Waveshare board.<br>
      2. Tap and release <strong>RESET</strong> while keeping BOOT held.<br>
      3. Release <strong>BOOT</strong>, then run <code>west flash</code>.<br>
      4. Press <strong>RESET</strong> once flashing completes. Deferred USB CDC-ACM logging ensures boot never hangs even with no serial monitor attached.
    </div>
  `;
}

function renderVerification() {
  const testOut = `[PASS] test_rs485_t35_adaptive_and_transact
[PASS] test_can_ack_bus_off_and_recovery
[PASS] test_scanner_and_profiles
[PASS] test_500_cycle_power_cut_torture (500 cuts, 0 corruptions)
[PASS] test_7_day_high_rate_polling_simulation (3024000 records, 0 lost, 671 outages)
[PASS] test_5min_commissioning_web_and_ota
ALL PHASE 0 MATURE TESTS PASSED (6/6)`;

  return `
    <div class="view-header">
      <div class="eyebrow">PRD Acceptance Criteria • tests/test_runner.c</div>
      <h1>Verification &amp; Torture Test Results</h1>
      <p>Compiled with <code>-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined</code> covering all 3 Phase 0 PRD acceptance criteria.</p>
    </div>

    ${codeBlock("Verified Output (./run_tests.sh)", testOut)}

    <div class="grid-3">
      <div class="card">
        <span class="badge badge-ok">Criterion #1</span>
        <h3 style="margin-top:8px">500-Cycle Power-Cut Torture</h3>
        <p>Injects torn superblock writes and mid-record data corruptions across 500 cold-remount cycles on persistent POSIX storage + NVS crash-reboot rollback. Result: <strong>0 corruptions, 0 lost committed records</strong>.</p>
      </div>
      <div class="card">
        <span class="badge badge-ok">Criterion #2</span>
        <h3 style="margin-top:8px">7-Day High-Rate Simulation</h3>
        <p>Simulates 604,800 seconds polling 4 Modbus slaves + 1 CAN BMS at 1s intervals with Wi-Fi drops every 15 minutes (671 outages). Result: <strong>3,024,000 records published, 0 lost</strong>.</p>
      </div>
      <div class="card">
        <span class="badge badge-ok">Criterion #3</span>
        <h3 style="margin-top:8px">5-Min Commissioning &amp; OTA</h3>
        <p>Exercises SoftAP Web UI, auto-scan, JK BMS profile load, BLE radio shutdown, 60s watchdog confirmation, chunked OTA CRC-32 verification, and 30s sustained health gate.</p>
      </div>
    </div>
  `;
}

const ROUTES = {
  overview: renderOverview,
  hardware: renderHardware,
  architecture: renderArchitecture,
  simulator: renderSimulator,
  profiles: renderProfiles,
  api: renderApi,
  deployment: renderDeployment,
  verification: renderVerification
};

let currentRoute = "overview";

function renderRoute(route) {
  currentRoute = ROUTES[route] ? route : "overview";
  document.getElementById("appRoot").innerHTML = ROUTES[currentRoute]();
  document.querySelectorAll(".nav-item").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.route === currentRoute);
  });
  if (window.scrollTo) window.scrollTo(0, 0);
}

window.navigate = function (route) {
  if (location.hash !== "#" + route) {
    history.replaceState(null, "", "#" + route);
  }
  renderRoute(route);
  document.getElementById("sidebar").classList.remove("open");
};

/* --- Theme, Mobile Drawer & Command Palette --- */
document.addEventListener("DOMContentLoaded", () => {
  const savedTheme = localStorage.getItem("eb_docs_theme") || "dark";
  if (savedTheme === "light") document.documentElement.setAttribute("data-theme", "light");

  document.getElementById("themeToggle").addEventListener("click", () => {
    const isLight = document.documentElement.getAttribute("data-theme") === "light";
    document.documentElement.setAttribute("data-theme", isLight ? "dark" : "light");
    localStorage.setItem("eb_docs_theme", isLight ? "dark" : "light");
  });

  document.getElementById("menuToggle").addEventListener("click", () => {
    document.getElementById("sidebar").classList.toggle("open");
  });

  document.querySelectorAll(".nav-item").forEach((btn) => {
    btn.addEventListener("click", () => navigate(btn.dataset.route));
  });

  const modal = document.getElementById("searchModal");
  const input = document.getElementById("searchInput");
  const results = document.getElementById("searchResults");

  function openSearch() {
    modal.classList.add("open");
    input.value = "";
    renderSearch("");
    setTimeout(() => input.focus(), 20);
  }

  function renderSearch(q) {
    const term = q.trim().toLowerCase();
    const hits = SEARCH_INDEX.filter((item) =>
      !term || item.title.toLowerCase().includes(term) || item.tags.includes(term)
    );
    results.innerHTML = hits.map((h) => `
      <button class="search-result" onclick="jumpSearch('${h.route}')">
        <span>${escHtml(h.title)}</span>
        <span class="badge badge-info">${h.route}</span>
      </button>
    `).join("") || `<div style="padding:14px;color:var(--text-muted)">No matching documentation entries.</div>`;
  }

  window.jumpSearch = function (route) {
    modal.classList.remove("open");
    navigate(route);
  };

  document.getElementById("searchTrigger").addEventListener("click", openSearch);
  modal.addEventListener("click", (e) => { if (e.target === modal) modal.classList.remove("open"); });
  input.addEventListener("input", () => renderSearch(input.value));
  document.addEventListener("keydown", (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "k") {
      e.preventDefault();
      openSearch();
    } else if (e.key === "Escape") {
      modal.classList.remove("open");
    }
  });

  const initial = location.hash.replace("#", "") || "overview";
  renderRoute(initial);
});
