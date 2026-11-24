/* EdgeBridge-S3 Phase 0 — Production Documentation App */

const ROUTE_ORDER = [
  { id: "overview", title: "Overview & Quirks Solved" },
  { id: "hardware", title: "Hardware Pinout & Flash" },
  { id: "architecture", title: "RTOS & Self-Healing" },
  { id: "simulator", title: "Bus Analyzer & t3.5 Lab" },
  { id: "profiles", title: "Device Profiles & Validator" },
  { id: "api", title: "REST API & MQTT" },
  { id: "deployment", title: "Build, Flash & USB Trap" },
  { id: "verification", title: "Verification & Torture Tests" },
  { id: "licensing", title: "Licensing & Build BOM" }
];

const PIN_DETAILS = [
  { id: "rs485_tx", name: "RS-485 TX", gpio: "GPIO17", iface: "UART1", notes: "Isolated RS-485 transmit line. Hardware auto-direction switches TX/RX automatically; practical ceiling capped at 115,200 bps due to isolator propagation delay." },
  { id: "rs485_rx", name: "RS-485 RX", gpio: "GPIO18", iface: "UART1", notes: "Isolated RS-485 receive line with internal pull-up and ISR ring queue (k_msgq) flushed before every Modbus RTU TX." },
  { id: "rs485_de", name: "RS-485 DE/EN (Optional)", gpio: "GPIO21", iface: "GPIO", notes: "Unused in Waveshare hardware auto-direction default (rs485_auto_direction=true). Toggleable if wired to an external manual DE/RE transceiver." },
  { id: "can_tx", name: "CAN (TWAI) TX", gpio: "GPIO15", iface: "TWAI", notes: "Isolated CAN 2.0B transmit line. Paired with real-time TEC/REC & ACK tracking in eb_can.c to eliminate CAN LED ambiguity." },
  { id: "can_rx", name: "CAN (TWAI) RX", gpio: "GPIO16", iface: "TWAI", notes: "Isolated CAN 2.0B receive line with accept-all filter feeding Pylontech/Victron/JK-CAN frame decoder." },
  { id: "usb_cdc", name: "USB-C Native CDC-ACM", gpio: "GPIO19 / GPIO20", iface: "USB OTG", notes: "Native USB-C console with CONFIG_LOG_MODE_DEFERRED=y and CONFIG_UART_LINE_CTRL=n so boot never stalls without DTR." }
];

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
  { title: "Overview & Hardware Quirks Solved", route: "overview", tags: "phase 0 prd waveshare quirks ble sram usb comparison" },
  { title: "Waveshare Pinout Explorer (GPIO17, 18, 15, 16)", route: "hardware", tags: "pinout rs485 can twai usb cdc gpio21 120r antenna" },
  { title: "16 MB Flash Partition Layout (MCUboot + LittleFS)", route: "hardware", tags: "flash slot0 slot1 scratch nvs_config telemetry_fs" },
  { title: "3-Thread RTOS Architecture & PSRAM Offload", route: "architecture", tags: "threads poller storage mqtt web analyzer sram psram" },
  { title: "Dual-Superblock Storage & 60s Config Watchdog", route: "architecture", tags: "power cut littlefs crc32 nvs rollback watchdog" },
  { title: "Interactive Bus Analyzer & t3.5 Calculator", route: "simulator", tags: "calculator baud turnaround simulator diagnostics can ack" },
  { title: "Built-In Profiles & JSON Validator", route: "profiles", tags: "json profile modbus can registers decoder validate" },
  { title: "REST API & MQTT Telemetry Reference", route: "api", tags: "/api/v1/diagnostics /api/v1/scan /api/v1/config mqtt curl" },
  { title: "Build, Flash & USB-C Bootloader Recovery", route: "deployment", tags: "west build flash boot reset cdc acm west.yml" },
  { title: "Verification Suite (500-Cut Torture & 7-Day Sim)", route: "verification", tags: "tests asan ubsan torture simulation 3024000" },
  { title: "GNU GPL v3.0 + Commercial Dual-License & BOM", route: "licensing", tags: "license gplv3 commercial oem apache bsd picolibc hal_espressif" }
];

const simState = {
  baud: 115200,
  guardExtraUs: 250,
  strictCap: true,
  canFault: "healthy",
  watchdogSec: 0,
  watchdogTimer: null,
  scanDone: false,
  selectedPin: 0,
  selectedPart: 5,
  activeProfile: "jk_bms_modbus_v1",
  customProfileJson: JSON.stringify(BUILTIN_PROFILES.jk_bms_modbus_v1, null, 2),
  customProfileMsg: null,
  checklist: [false, false, false, false, false],
  searchIdx: 0
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
  setTimeout(() => t.classList.remove("show"), 1900);
}

/* --- View Renderers --- */

function renderOverview() {
  const doneCount = simState.checklist.filter(Boolean).length;
  return `
    <section class="hero">
      <div>
        <div class="eyebrow">Phase 0 • v0.1.0-phase0 • Zero-Capex Deployment</div>
        <h1>Self-Healing RS-485 &amp; CAN Edge Gateway</h1>
        <p>Hardened Zephyr RTOS + MCUboot + LittleFS firmware for the off-the-shelf Waveshare <code>ESP32-S3-RS485-CAN</code>. Solves auto-direction RS-485 clipping, BLE+Wi-Fi SRAM crashes, CAN LED ambiguity, and power-cut storage corruption.</p>
        <div class="form-row">
          <button class="btn" onclick="navigate('simulator')">Launch Bus Analyzer Lab</button>
          <button class="btn btn-sec" onclick="navigate('hardware')">Inspect Pinout &amp; Flash</button>
          <a class="btn btn-sec" href="https://github.com/nitin-chauhan66/edgebridge/releases/tag/v0.1.0" target="_blank" rel="noopener">v0.1.0 Release</a>
        </div>
      </div>
      <div class="hero-panel">
        <div class="hero-kv"><span>Target Board</span><strong>Waveshare S3 (₹2,049)</strong></div>
        <div class="hero-kv"><span>MCU / Memory</span><strong>ESP32-S3R8 (16M / 8M PSRAM)</strong></div>
        <div class="hero-kv"><span>RS-485 Ceiling</span><strong>115,200 bps + t3.5 Guard</strong></div>
        <div class="hero-kv"><span>Power-Cut Test</span><strong>500 / 500 Passed (0 lost)</strong></div>
        <div class="hero-kv"><span>7-Day Polling Sim</span><strong>3,024,000 records (0 lost)</strong></div>
        <div class="hero-kv"><span>License Model</span><strong>GPL-3.0 / Commercial OEM</strong></div>
      </div>
    </section>

    <h2 class="section-title">Raw Waveshare Demos vs. EdgeBridge-S3 Phase 0</h2>
    <div class="table-wrap">
      <table>
        <thead><tr><th>Field Challenge</th><th>Stock Arduino / IDF Demo</th><th>EdgeBridge-S3 Phase 0 Solution</th></tr></thead>
        <tbody>
          <tr><td><strong>Auto-Dir RS-485 Clipping</strong></td><td>Fixed delays drop tail bytes above 38.4k bps</td><td>Strict <code>t3.5</code> guard + 115.2k isolator ceiling + adaptive turnaround escalation (<code>eb_rs485.c</code>)</td></tr>
          <tr><td><strong>BLE + Wi-Fi SRAM Crash</strong></td><td>Internal SRAM exhausted; random panics</td><td>Large buffers offloaded to 8 MB Octal PSRAM (<code>.ext_ram.bss</code>) + <code>bt_disable()</code> post-setup</td></tr>
          <tr><td><strong>CAN LED Ambiguity</strong></td><td>TX &amp; RX blink together even with no peer ACK</td><td>Real-time <code>TEC</code>/<code>REC</code>, ACK-OK vs Missing-ACK, Error-Passive/Bus-Off + recovery (<code>eb_can.c</code>)</td></tr>
          <tr><td><strong>Power-Cut Corruption</strong></td><td>In-RAM queues lost; unguarded flash writes tear</td><td>Dual alternating superblocks + IEEE CRC-32 + crash replay on LittleFS (<code>eb_storage.c</code>)</td></tr>
          <tr><td><strong>Remote Config Lockout</strong></td><td>Bad Wi-Fi or baud setting bricks remote node</td><td>60s NVS watchdog + automatic rollback on reboot if unconfirmed (<code>eb_config.c</code>)</td></tr>
        </tbody>
      </table>
    </div>

    <h2 class="section-title">Interactive 5-Minute Field Commissioning Checklist (${doneCount}/5)</h2>
    <div class="card">
      ${[
        "Inspect Waveshare 120Ω jumper caps inside DIN case (120R at bus endpoints, NC mid-bus) and verify antenna resistor path.",
        "Wire RS-485 A+/B- and/or CANH/CANL + 9–36V DC supply and power on.",
        "Connect phone to EdgeBridge-S3-AP or BLE provisioning and open http://192.168.4.1/.",
        "Click Auto-Scan (POST /api/v1/scan) to discover RS-485 baud/slave IDs & CAN bitrate, then load BMS/Inverter JSON profile.",
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
  const pin = PIN_DETAILS[simState.selectedPin] || PIN_DETAILS[0];
  const part = FLASH_PARTITIONS[simState.selectedPart] || FLASH_PARTITIONS[5];

  return `
    <div class="view-header">
      <div class="eyebrow">Hardware &amp; DeviceTree • boards/waveshare_esp32s3_rs485_can.overlay</div>
      <h1>Waveshare Pinout Explorer &amp; 16 MB Flash Map</h1>
      <p>Interactive hardware reference for the Waveshare ESP32-S3-RS485-CAN DIN-rail board (ESP32-S3R8: 16 MB Flash + 8 MB Octal PSRAM).</p>
    </div>

    <h2 class="section-title">Interactive Pinout &amp; Bus Terminal Explorer</h2>
    <div class="board-wrap">
      <div class="card">
        <svg viewBox="0 0 460 230" width="100%" aria-label="Waveshare ESP32-S3-RS485-CAN Block Diagram" style="display:block;background:var(--md-sys-color-code-bg);border-radius:16px;border:1px solid var(--md-sys-color-outline-variant)">
          <rect x="145" y="34" width="170" height="162" rx="14" fill="#1e222a" stroke="#a8c7fa" stroke-width="2"/>
          <text x="230" y="64" text-anchor="middle" fill="#e3e2e6" font-size="13" font-weight="500">ESP32-S3R8</text>
          <text x="230" y="82" text-anchor="middle" fill="#c4c6d0" font-size="10">16MB Flash • 8MB PSRAM</text>
          <rect x="165" y="100" width="130" height="32" rx="8" fill="#0f5223" stroke="#6dd58c"/>
          <text x="230" y="120" text-anchor="middle" fill="#c4eed0" font-size="10" font-weight="500">PSRAM (.ext_ram.bss)</text>
          <rect x="165" y="144" width="130" height="32" rx="8" fill="#282c36" stroke="#8e918f"/>
          <text x="230" y="164" text-anchor="middle" fill="#e3e2e6" font-size="10">USB-C CDC (GPIO19/20)</text>

          <!-- Left: RS485 -->
          <rect x="14" y="42" width="105" height="66" rx="12" fill="#0842a0" stroke="#a8c7fa"/>
          <text x="66" y="65" text-anchor="middle" fill="#d3e3fd" font-size="11" font-weight="500">Isolated RS-485</text>
          <text x="66" y="81" text-anchor="middle" fill="#e3e2e6" font-size="9.5">TX:17 RX:18 (DE:21)</text>
          <text x="66" y="96" text-anchor="middle" fill="#ffdf99" font-size="9">Auto-Dir • 120R Cap</text>
          <line x1="119" y1="75" x2="145" y2="75" stroke="#a8c7fa" stroke-width="2"/>

          <!-- Left: CAN -->
          <rect x="14" y="126" width="105" height="64" rx="12" fill="#0f5223" stroke="#6dd58c"/>
          <text x="66" y="149" text-anchor="middle" fill="#c4eed0" font-size="11" font-weight="500">Isolated CAN 2.0B</text>
          <text x="66" y="165" text-anchor="middle" fill="#e3e2e6" font-size="9.5">TWAI TX:15 RX:16</text>
          <text x="66" y="180" text-anchor="middle" fill="#ffdf99" font-size="9">120R Cap • ±40V Prot</text>
          <line x1="119" y1="158" x2="145" y2="158" stroke="#6dd58c" stroke-width="2"/>

          <!-- Right: RF & Power -->
          <rect x="341" y="42" width="105" height="66" rx="12" fill="#004a77" stroke="#7fcfff"/>
          <text x="393" y="65" text-anchor="middle" fill="#c2e7ff" font-size="11" font-weight="500">Wi-Fi / BLE 5</text>
          <text x="393" y="81" text-anchor="middle" fill="#e3e2e6" font-size="9.5">Ceramic / IPEX1</text>
          <text x="393" y="96" text-anchor="middle" fill="#ffdf99" font-size="9">0402 RF Resistor</text>
          <line x1="315" y1="75" x2="341" y2="75" stroke="#7fcfff" stroke-width="2"/>

          <rect x="341" y="126" width="105" height="64" rx="12" fill="#4a2c00" stroke="#fbbc04"/>
          <text x="393" y="150" text-anchor="middle" fill="#ffdf99" font-size="11" font-weight="500">7–36V DC Input</text>
          <text x="393" y="166" text-anchor="middle" fill="#e3e2e6" font-size="9.5">Isolated DIN Power</text>
          <text x="393" y="181" text-anchor="middle" fill="#e3e2e6" font-size="9">TVS + Resettable Fuse</text>
          <line x1="315" y1="158" x2="341" y2="158" stroke="#fbbc04" stroke-width="2"/>
        </svg>
      </div>

      <div class="card">
        <h3>Select Pin / Interface</h3>
        ${PIN_DETAILS.map((p, idx) => `
          <button class="pin-btn ${simState.selectedPin === idx ? "active" : ""}" onclick="selectPin(${idx})">
            <span><strong>${escHtml(p.name)}</strong> (${escHtml(p.iface)})</span>
            <code>${escHtml(p.gpio)}</code>
          </button>
        `).join("")}
        <div class="callout" style="margin-bottom:0">
          <strong>${escHtml(pin.name)} — <code>${escHtml(pin.gpio)}</code>:</strong><br>
          ${escHtml(pin.notes)}
        </div>
      </div>
    </div>

    <h2 class="section-title">Interactive 16 MB Flash Partition Map</h2>
    <div class="card">
      <p>Click any segment to inspect exact offsets and responsibilities:</p>
      <div class="flash-bar">
        ${FLASH_PARTITIONS.map((p, idx) => `
          <div class="flash-seg" style="width:${p.pct}%;background:${p.color}" onclick="selectPartition(${idx})" title="${p.label} (${p.size})">
            ${p.label}
          </div>
        `).join("")}
      </div>
      <div class="callout ok">
        <strong>${escHtml(part.name)}</strong> (<code>${part.offset}</code>, ${part.size}): ${escHtml(part.desc)}
      </div>
      <div class="table-wrap" style="margin-top:12px">
        <table>
          <thead><tr><th>Offset</th><th>Size</th><th>Partition</th><th>Purpose</th></tr></thead>
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

window.selectPin = function (idx) {
  simState.selectedPin = idx;
  renderRoute("hardware");
};

window.selectPartition = function (idx) {
  simState.selectedPart = idx;
  renderRoute("hardware");
};

function renderArchitecture() {
  return `
    <div class="view-header">
      <div class="eyebrow">Deterministic Runtime • src/main.c</div>
      <h1>3-Thread Architecture &amp; Self-Healing Design</h1>
      <p>Bus polling never blocks on Wi-Fi, TLS, flash writes, or HTTP requests. All threads communicate via bounded lock-free queues and CRC-protected storage.</p>
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
        <p>Serves HTTP Bus Analyzer (:80), runs 60s NVS config rollback watchdog, disables BLE via <code>bt_disable()</code>, gates MCUboot confirmation on sustained bus + network health, and feeds ESP32 WDT.</p>
      </div>
    </div>

    <h2 class="section-title">Memory Budget: PSRAM Offload (&lt; 35% Internal SRAM Target)</h2>
    <div class="grid-2">
      <div class="card">
        <h3>Internal SRAM (512 KB)</h3>
        <div class="meter"><div class="meter-fill" style="width:24%;background:var(--md-sys-color-tertiary)"></div></div>
        <p><strong>~24% used</strong> — RTOS kernel stacks, UART/CAN ISRs, and control structs only. Zero large telemetry buffers in internal DRAM.</p>
      </div>
      <div class="card">
        <h3>Octal PSRAM (8 MB • <code>.ext_ram.bss</code>)</h3>
        <div class="meter"><div class="meter-fill" style="width:18%;background:var(--md-sys-color-primary)"></div></div>
        <p><strong>8 MB available</strong> — Holds <code>g_queue_buf</code>, <code>g_profiles</code>, <code>g_http_resp_buf</code>, <code>g_mqtt_json_buf</code>, and mbedTLS heap.</p>
      </div>
    </div>

    <h2 class="section-title">Power-Cut &amp; Remote Lockout Guarantees</h2>
    <div class="grid-2">
      <div class="card">
        <h3>Dual-Superblock LittleFS Ring (<code>eb_storage.c</code>)</h3>
        <p>Alternates checkpoints between two superblock banks (<code>generation &amp; 1</code>) with IEEE CRC-32. On boot, selects the highest valid generation, replays valid-CRC slots appended after the last checkpoint, and skips half-written corrupt slots cleanly.</p>
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

  const canStateMap = {
    healthy: { badge: "badge-ok", state: "ERROR_ACTIVE", tec: 0, rec: 0, ackOk: 128, ackMiss: 0, busOff: 0, peerAck: true, hint: "Healthy: Peer ACKing and active CAN traffic verified" },
    no_ack: { badge: "badge-warn", state: "ERROR_PASSIVE", tec: 136, rec: 0, ackOk: 0, ackMiss: 42, busOff: 0, peerAck: false, hint: "No peer ACK (Error Passive): Check inverter/BMS power, CANH/CANL polarity, and 120R jumper" },
    bus_off: { badge: "badge-err", state: "BUS_OFF", tec: 255, rec: 12, ackOk: 0, ackMiss: 64, busOff: 2, peerAck: false, hint: "BUS_OFF: Check shorted CANH/CANL, 120R termination, or bitrate mismatch" }
  };
  const cs = canStateMap[simState.canFault];

  const liveDiag = {
    fw_version: "0.1.0-phase0",
    rs485: { baud: effectiveBaud, t35_us: t35, auto_dir: true, ceiling_warn: ceilingWarn, tx: 240, rx: 240, crc_err: 0, clipped_err: 0 },
    can: { bitrate: 500000, state: cs.state, tec: cs.tec, rec: cs.rec, ack_ok: cs.ackOk, ack_miss: cs.ackMiss, bus_off: cs.busOff, peer_ack: cs.peerAck },
    config: { version: 3, pending_confirm: simState.watchdogSec > 0, profile: simState.activeProfile },
    watchdog_remaining_sec: simState.watchdogSec,
    memory: { internal_sram_pct: 24, psram_active: true }
  };

  return `
    <div class="view-header">
      <div class="eyebrow">Interactive Lab • eb_rs485.c &amp; eb_web_analyzer.c</div>
      <h1>Bus Analyzer Sandbox &amp; t3.5 Calculator</h1>
      <p>Test Waveshare RS-485 isolator timing limits, CAN ACK/Bus-Off diagnostics, Auto-Scan, and live <code>/api/v1/diagnostics</code> responses.</p>
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
        <div class="callout ${ceilingWarn ? "warn" : "ok"}" style="margin-bottom:0">
          <strong>Effective Baud:</strong> <code>${effectiveBaud.toLocaleString()} bps</code>
          ${ceilingWarn ? (simState.strictCap ? "(Clamped from " + simState.baud.toLocaleString() + " — Waveshare isolator ceiling)" : "(Warning: exceeds 115,200 bps isolator limit)") : "(Within verified isolator limit)"}<br>
          <strong>t3.5 Inter-Frame Guard:</strong> <code>${t35} µs</code> | <strong>8-byte TX Time:</strong> <code>${frameTxUs} µs</code>
        </div>
      </div>

      <div class="card">
        <h3>Web Bus Analyzer Controls</h3>
        <div class="form-row" style="margin:10px 0">
          <label>CAN Fault Mode:
            <select id="simCanSel" onchange="updateCanSim(this.value)">
              <option value="healthy" ${simState.canFault === "healthy" ? "selected" : ""}>Healthy (Peer ACK)</option>
              <option value="no_ack" ${simState.canFault === "no_ack" ? "selected" : ""}>Missing Peer ACK</option>
              <option value="bus_off" ${simState.canFault === "bus_off" ? "selected" : ""}>Bus-Off Fault</option>
            </select>
          </label>
          <button class="btn" onclick="simRunScan()">POST /api/v1/scan</button>
          <button class="btn btn-warn" onclick="simStageCfg()">Stage Config (60s)</button>
          ${simState.watchdogSec > 0 ? `<button class="btn btn-ok" onclick="simConfirmCfg()">Confirm (${simState.watchdogSec}s)</button>` : ""}
        </div>
        <div style="margin-top:8px">
          <span class="badge ${cs.badge}">${cs.state} • TEC:${cs.tec} REC:${cs.rec}</span>
          <p style="margin-top:8px">${escHtml(cs.hint)}</p>
          ${simState.scanDone ? `<p style="margin-top:8px;color:var(--md-sys-color-tertiary)"><strong>Auto-Scan Hit:</strong> RS-485 Slave #1 @ 115,200 bps (JK-PB BMS) &amp; CAN @ 500 kbps (IDs 0x351, 0x355, 0x356)</p>` : ""}
        </div>
      </div>
    </div>

    <div style="margin-top:16px">
      ${codeBlock("Live Simulated GET /api/v1/diagnostics", JSON.stringify(liveDiag, null, 2))}
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
  showToast("Auto-scan complete: RS-485 Slave #1 & CAN 500k detected");
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
  showToast("Configuration confirmed and saved to NVS backup bank");
  renderRoute("simulator");
};

function renderProfiles() {
  const keys = Object.keys(BUILTIN_PROFILES);
  const p = BUILTIN_PROFILES[simState.activeProfile] || BUILTIN_PROFILES.jk_bms_modbus_v1;
  return `
    <div class="view-header">
      <div class="eyebrow">Profile Engine • src/eb_profile.c &amp; profiles/</div>
      <h1>Built-In Device Profiles &amp; JSON Validator</h1>
      <p>Zero-allocation JSON parser supports up to 5 concurrent profiles (e.g., 4 Modbus slaves + 1 CAN BMS at 1s polling).</p>
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
        <div class="form-row" style="margin-top:12px">
          <button class="btn" onclick="downloadActiveProfile()">Download ${escHtml(p.profile_id)}.json</button>
          <button class="btn btn-sec" onclick="loadIntoValidator()">Edit in Validator</button>
        </div>
      </div>
      <div>
        ${codeBlock(p.profile_id + ".json", JSON.stringify(p, null, 2))}
      </div>
    </div>

    <h2 class="section-title">Custom JSON Profile Validator</h2>
    <div class="card">
      <p style="margin-bottom:10px">Paste or edit a custom BMS/inverter profile JSON before uploading via <code>POST /api/v1/profiles</code>:</p>
      <textarea class="code-input" id="profEditor" aria-label="Custom profile JSON editor">${escHtml(simState.customProfileJson)}</textarea>
      <div class="form-row" style="margin-top:10px">
        <button class="btn" onclick="validateCustomProfile()">Validate Profile JSON</button>
      </div>
      ${simState.customProfileMsg ? `<div class="callout ${simState.customProfileMsg.type}" style="margin-bottom:0">${escHtml(simState.customProfileMsg.text)}</div>` : ""}
    </div>
  `;
}

window.selectProfile = function (k) {
  simState.activeProfile = k;
  renderRoute("profiles");
};

window.downloadActiveProfile = function () {
  const p = BUILTIN_PROFILES[simState.activeProfile] || BUILTIN_PROFILES.jk_bms_modbus_v1;
  const blob = new Blob([JSON.stringify(p, null, 2)], { type: "application/json" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = p.profile_id + ".json";
  a.click();
  URL.revokeObjectURL(a.href);
};

window.loadIntoValidator = function () {
  const p = BUILTIN_PROFILES[simState.activeProfile] || BUILTIN_PROFILES.jk_bms_modbus_v1;
  simState.customProfileJson = JSON.stringify(p, null, 2);
  simState.customProfileMsg = null;
  renderRoute("profiles");
};

window.validateCustomProfile = function () {
  const txt = document.getElementById("profEditor").value;
  simState.customProfileJson = txt;
  try {
    const obj = JSON.parse(txt);
    if (!obj.profile_id || typeof obj.profile_id !== "string") throw new Error("Missing string 'profile_id'.");
    if (!Array.isArray(obj.registers) || obj.registers.length === 0) throw new Error("'registers' must be a non-empty array.");
    if (obj.registers.length > 16) throw new Error("Exceeds EB_PROFILE_MAX_METRICS (16 signals max).");
    if (obj.bus !== "can" && Number(obj.default_baud) > 115200) {
      simState.customProfileMsg = { type: "warn", text: "Valid JSON, but RS-485 baud " + obj.default_baud + " exceeds Waveshare's 115,200 bps isolator ceiling and will be clamped when strict_cap=true." };
    } else {
      simState.customProfileMsg = { type: "ok", text: "Valid EdgeBridge-S3 profile '" + obj.profile_id + "' (" + obj.registers.length + " signals). Ready for POST /api/v1/profiles." };
    }
  } catch (e) {
    simState.customProfileMsg = { type: "err", text: "Invalid profile: " + e.message };
  }
  renderRoute("profiles");
};

function renderApi() {
  const diagSample = `{
  "fw_version": "0.1.0-phase0",
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
          <tr><td><code>GET /api/v1/diagnostics</code></td><td>Firmware version, RS-485, CAN, LittleFS, Watchdog, BLE, MQTT, OTA &amp; SRAM/PSRAM JSON</td></tr>
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

    ${codeBlock("Quick Commissioning via curl", `curl -X POST http://192.168.4.1/api/v1/scan\ncurl -X POST http://192.168.4.1/api/v1/profiles -d '{"builtin":"jk_bms_modbus_v1"}'\ncurl -X POST http://192.168.4.1/api/v1/config -d '{"wifi_ssid":"PlantNet","wifi_pass":"secret","mqtt_broker":"mqtt.local","mqtt_port":1883}'\ncurl -X POST http://192.168.4.1/api/v1/config/confirm`)}
  `;
}

function renderDeployment() {
  return `
    <div class="view-header">
      <div class="eyebrow">Build, Flash &amp; Field Recovery • west.yml &amp; sysbuild.conf</div>
      <h1>Building, Flashing &amp; USB-C Boot Trap</h1>
      <p>Initialize the Zephyr v3.7.0 workspace, sign with ECDSA-P256, flash the Waveshare ESP32-S3-RS485-CAN, or run host verification tests.</p>
    </div>

    <h2 class="section-title">1. Initialize Workspace &amp; Build Zephyr Firmware</h2>
    ${codeBlock("Zephyr v3.7.0 + MCUboot Sysbuild", `west init -l .\nwest update\nwest blobs fetch hal_espressif\n\nchmod +x scripts/gen_dev_keys.sh scripts/build_phase0.sh\n./scripts/build_phase0.sh\nwest flash`)}

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
      <p>Compiled with <code>-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined</code> and verified in CI on every push.</p>
    </div>

    ${codeBlock("Verified Output (./tests/run_tests.sh)", testOut)}

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

function renderLicensing() {
  return `
    <div class="view-header">
      <div class="eyebrow">Dual License &amp; Supply Chain • LICENSE &amp; COMMERCIAL_LICENSE.md</div>
      <h1>Licensing &amp; Aligned ESP32-S3 Build BOM</h1>
      <p>Free for developers and open-source GPLv3 projects; commercial OEM licensing available for proprietary product distribution.</p>
    </div>

    <div class="grid-2">
      <div class="card">
        <span class="badge badge-ok">Developers &amp; Open Source</span>
        <h3 style="margin-top:8px">GNU GPL v3.0 (<code>GPL-3.0-only</code>)</h3>
        <p>Free to use, study, modify, and distribute under GPL-3.0. Includes a GPLv3 Section 7 permission allowing linking and distributing unmodified Espressif <code>hal_espressif</code> SoC Wi-Fi/BLE PHY libraries.</p>
      </div>
      <div class="card">
        <span class="badge badge-info">Organizations &amp; OEMs</span>
        <h3 style="margin-top:8px">Commercial License (<code>LicenseRef-EdgeBridge-Commercial</code>)</h3>
        <p>Required for organizations embedding EdgeBridge-S3 into closed-source commercial products or custom OEM fleets without GPLv3 source-disclosure obligations.</p>
      </div>
    </div>

    <h2 class="section-title">Verified Upstream Component BOM (<code>west.yml</code>)</h2>
    <div class="table-wrap">
      <table>
        <thead><tr><th>Component</th><th>Upstream License</th><th>GPL-3.0 Status</th><th>Role</th></tr></thead>
        <tbody>
          <tr><td><strong>Zephyr RTOS (v3.7.0)</strong></td><td><code>Apache-2.0</code></td><td>Compatible</td><td>Deterministic 3-thread RTOS, UART, TWAI, USB CDC, Wi-Fi/MQTT, NVS</td></tr>
          <tr><td><strong>hal_espressif</strong></td><td><code>Apache-2.0</code></td><td>Compatible + Sec. 7</td><td>ESP32-S3 HAL, 8 MB Octal PSRAM, Wi-Fi/BT PHY libraries</td></tr>
          <tr><td><strong>MCUboot</strong></td><td><code>Apache-2.0</code></td><td>Compatible</td><td>Dual-slot bootloader with ECDSA-P256 verification</td></tr>
          <tr><td><strong>Mbed TLS</strong></td><td><code>Apache-2.0 OR GPL-2.0+</code></td><td>Compatible</td><td>MQTT TLS &amp; cryptographic primitives (PSRAM heap)</td></tr>
          <tr><td><strong>zcbor</strong></td><td><code>Apache-2.0</code></td><td>Compatible</td><td>CBOR codec for Zephyr image management</td></tr>
          <tr><td><strong>LittleFS</strong></td><td><code>BSD-3-Clause</code></td><td>Compatible</td><td>Power-cut-safe flash filesystem for <code>/telemetry_fs</code></td></tr>
          <tr><td><strong>picolibc</strong></td><td><code>BSD-3-Clause</code></td><td>Compatible</td><td>Zephyr v3.7.0 default C runtime library</td></tr>
          <tr><td><strong>TinyCrypt (0.2.8)</strong></td><td><code>BSD-3-Clause / BSD-2-Clause</code></td><td>Compatible</td><td>SHA-256 &amp; ECC primitives for MCUboot</td></tr>
        </tbody>
      </table>
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
  verification: renderVerification,
  licensing: renderLicensing
};

let currentRoute = "overview";

function renderPager(route) {
  const idx = ROUTE_ORDER.findIndex((r) => r.id === route);
  if (idx < 0) return "";
  const prev = idx > 0 ? ROUTE_ORDER[idx - 1] : null;
  const next = idx + 1 < ROUTE_ORDER.length ? ROUTE_ORDER[idx + 1] : null;
  return `
    <nav class="doc-pager" aria-label="Page navigation">
      ${prev ? `<button class="pager-btn" onclick="navigate('${prev.id}')"><span class="pager-sub">&larr; Previous</span><span class="pager-title">${escHtml(prev.title)}</span></button>` : `<span></span>`}
      ${next ? `<button class="pager-btn next" onclick="navigate('${next.id}')"><span class="pager-sub">Next &rarr;</span><span class="pager-title">${escHtml(next.title)}</span></button>` : ""}
    </nav>
  `;
}

function renderRoute(route) {
  currentRoute = ROUTES[route] ? route : "overview";
  const html = ROUTES[currentRoute]() + renderPager(currentRoute);
  document.getElementById("appRoot").innerHTML = html;
  document.querySelectorAll(".nav-item").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.route === currentRoute);
  });
  if (window.scrollTo) window.scrollTo(0, 0);
}

function setDrawerOpen(open) {
  const sb = document.getElementById("sidebar");
  const bd = document.getElementById("drawerBackdrop");
  if (sb && sb.classList) sb.classList.toggle("open", open);
  if (bd && bd.classList) bd.classList.toggle("open", open);
}

window.navigate = function (route) {
  if (location.hash !== "#" + route) {
    history.replaceState(null, "", "#" + route);
  }
  renderRoute(route);
  setDrawerOpen(false);
};

/* --- Theme, Mobile Drawer & Keyboard-Navigable Command Palette --- */
document.addEventListener("DOMContentLoaded", () => {
  const themeBtn = document.getElementById("themeToggle");
  function applyTheme(mode) {
    if (mode === "dark") {
      document.documentElement.setAttribute("data-theme", "dark");
      themeBtn.textContent = "Light";
    } else {
      document.documentElement.removeAttribute("data-theme");
      themeBtn.textContent = "Dark";
    }
  }
  const savedTheme = localStorage.getItem("eb_docs_theme") || "light";
  applyTheme(savedTheme);

  themeBtn.addEventListener("click", () => {
    const isDark = document.documentElement.getAttribute("data-theme") === "dark";
    const next = isDark ? "light" : "dark";
    applyTheme(next);
    localStorage.setItem("eb_docs_theme", next);
  });

  document.getElementById("menuToggle").addEventListener("click", () => {
    const isOpen = document.getElementById("sidebar").classList.contains("open");
    setDrawerOpen(!isOpen);
  });

  document.getElementById("drawerBackdrop").addEventListener("click", () => {
    setDrawerOpen(false);
  });

  document.querySelectorAll(".nav-item").forEach((btn) => {
    btn.addEventListener("click", () => navigate(btn.dataset.route));
  });

  const modal = document.getElementById("searchModal");
  const input = document.getElementById("searchInput");
  const results = document.getElementById("searchResults");
  let filteredHits = [];

  function openSearch() {
    modal.classList.add("open");
    input.value = "";
    simState.searchIdx = 0;
    renderSearch("");
    setTimeout(() => input.focus(), 20);
  }

  function renderSearch(q) {
    const term = q.trim().toLowerCase();
    filteredHits = SEARCH_INDEX.filter((item) =>
      !term || item.title.toLowerCase().includes(term) || item.tags.includes(term)
    );
    if (simState.searchIdx >= filteredHits.length) simState.searchIdx = 0;
    results.innerHTML = filteredHits.map((h, i) => `
      <button class="search-result ${i === simState.searchIdx ? "selected" : ""}" onclick="jumpSearch('${h.route}')">
        <span>${escHtml(h.title)}</span>
        <span class="badge badge-info">${h.route}</span>
      </button>
    `).join("") || `<div style="padding:14px;color:var(--md-sys-color-on-surface-variant)">No matching documentation entries.</div>`;
  }

  window.jumpSearch = function (route) {
    modal.classList.remove("open");
    navigate(route);
  };

  document.getElementById("searchTrigger").addEventListener("click", openSearch);
  modal.addEventListener("click", (e) => { if (e.target === modal) modal.classList.remove("open"); });
  input.addEventListener("input", () => { simState.searchIdx = 0; renderSearch(input.value); });

  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown" && filteredHits.length > 0) {
      e.preventDefault();
      simState.searchIdx = (simState.searchIdx + 1) % filteredHits.length;
      renderSearch(input.value);
    } else if (e.key === "ArrowUp" && filteredHits.length > 0) {
      e.preventDefault();
      simState.searchIdx = (simState.searchIdx - 1 + filteredHits.length) % filteredHits.length;
      renderSearch(input.value);
    } else if (e.key === "Enter" && filteredHits[simState.searchIdx]) {
      e.preventDefault();
      window.jumpSearch(filteredHits[simState.searchIdx].route);
    }
  });

  document.addEventListener("keydown", (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "k") {
      e.preventDefault();
      openSearch();
    } else if (e.key === "Escape") {
      modal.classList.remove("open");
      setDrawerOpen(false);
    }
  });

  const initial = location.hash.replace("#", "") || "overview";
  renderRoute(initial);
});
