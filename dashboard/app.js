// ============================================================
// Smart Adaptive Street Light Controller — Dashboard logic
// Talks to the Arduino over the Web Serial API using the
// newline-delimited protocol defined in the PRD (Section 14).
// ============================================================

let port = null;
let reader = null;
let writer = null;
let keepReading = false;
let lineBuffer = "";

const trafficHistory = []; // rolling window of recent traffic levels
const HISTORY_LENGTH = 30;

const state = {
  dayNight: null,
  ir: [false, false, false],
  traffic: "LOW",
  led: [0, 0, 0],
  mode: "AUTO",
  emergency: false,
  weather: "CLEAR",
  direction: "NONE",
  predicted: "NONE",
  detections: 0,
};

// ---------- Web Serial connection ----------

const connectBtn = document.getElementById("connectBtn");
const connDot = document.getElementById("connDot");
const connLabel = document.getElementById("connLabel");

connectBtn.addEventListener("click", async () => {
  if (port) {
    await disconnectSerial();
  } else {
    await connectSerial();
  }
});

async function connectSerial() {
  if (!("serial" in navigator)) {
    alert("Web Serial API is not available in this browser. Use a Chromium-based browser (Chrome, Edge, Brave) and open this file over http:// or as a local file — some browsers require a secure context.");
    return;
  }
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 9600 });

    const textDecoder = new TextDecoderStream();
    port.readable.pipeTo(textDecoder.writable);
    reader = textDecoder.readable.getReader();

    const textEncoder = new TextEncoderStream();
    textEncoder.readable.pipeTo(port.writable);
    writer = textEncoder.writable.getWriter();

    keepReading = true;
    setConnectionUI(true);
    readLoop();
  } catch (err) {
    console.error("Serial connection failed:", err);
    setConnectionUI(false);
    port = null;
  }
}

async function disconnectSerial() {
  keepReading = false;
  try {
    if (reader) { await reader.cancel(); reader.releaseLock(); }
    if (writer) { await writer.close(); }
    if (port) { await port.close(); }
  } catch (err) {
    console.error("Error while disconnecting:", err);
  }
  port = null;
  reader = null;
  writer = null;
  setConnectionUI(false);
}

async function readLoop() {
  while (keepReading) {
    try {
      const { value, done } = await reader.read();
      if (done) break;
      if (value) {
        lineBuffer += value;
        let idx;
        while ((idx = lineBuffer.indexOf("\n")) >= 0) {
          const line = lineBuffer.slice(0, idx).trim();
          lineBuffer = lineBuffer.slice(idx + 1);
          if (line.length > 0) handleIncomingLine(line);
        }
      }
    } catch (err) {
      console.error("Serial read error:", err);
      break;
    }
  }
  if (keepReading) {
    // Port dropped unexpectedly (e.g. cable unplugged)
    setConnectionUI(false);
    port = null;
  }
}

async function sendCommand(cmd) {
  if (!writer) return;
  try {
    await writer.write(cmd + "\n");
  } catch (err) {
    console.error("Failed to send command:", err);
  }
}

function setConnectionUI(connected) {
  connDot.classList.toggle("online", connected);
  connDot.classList.toggle("offline", !connected);
  connLabel.textContent = connected ? "Connected" : "Not connected";
  connectBtn.textContent = connected ? "Disconnect" : "Connect Arduino";
}

// ---------- Protocol parsing ----------

function handleIncomingLine(line) {
  if (line === "---") { renderAll(); return; } // frame separator from firmware
  const sep = line.indexOf(":");
  if (sep === -1) return;
  const key = line.slice(0, sep);
  const value = line.slice(sep + 1);

  switch (key) {
    case "DAYNIGHT": state.dayNight = value; break;
    case "IR1": state.ir[0] = value === "1"; break;
    case "IR2": state.ir[1] = value === "1"; break;
    case "IR3": state.ir[2] = value === "1"; break;
    case "TRAFFIC": state.traffic = value; break;
    case "LED1": state.led[0] = parseInt(value, 10) || 0; break;
    case "LED2": state.led[1] = parseInt(value, 10) || 0; break;
    case "LED3": state.led[2] = parseInt(value, 10) || 0; break;
    case "MODE": state.mode = value; break;
    case "EMERGENCY": state.emergency = value === "1"; break;
    case "WEATHER": state.weather = value; break;
    case "DIRECTION": state.direction = value; break;
    case "PREDICTED": state.predicted = value; break;
    case "DETECTIONS": state.detections = parseInt(value, 10) || 0; break;
  }
}

// ---------- Rendering ----------

function renderAll() {
  renderStatusBar();
  renderPredictivePage();
  renderTrafficPage();
  renderEmergencyPage();
  renderWeatherPage();
}

function renderStatusBar() {
  document.getElementById("statMode").textContent = state.emergency ? "EMERGENCY" : state.mode;
  document.getElementById("statDayNight").textContent = state.dayNight || "—";
  document.getElementById("statWeather").textContent = state.weather;

  for (let i = 0; i < 3; i++) {
    const fill = document.getElementById("miniLed" + (i + 1));
    fill.style.opacity = state.led[i] / 100;
  }

  const quickBtn = document.getElementById("quickEmergencyBtn");
  quickBtn.classList.toggle("active", state.emergency);
  quickBtn.textContent = state.emergency ? "Emergency active" : "Emergency";
}

function renderPredictivePage() {
  const zoneNames = { "1": "Zone 1", "2": "Zone 2", "3": "Zone 3", "NONE": "None" };

  const activeZone = state.ir.findIndex(v => v) + 1;
  document.getElementById("predDetectedZone").textContent = activeZone > 0 ? "Zone " + activeZone : "None";

  const dirText = state.direction === "NONE" ? "None" : `Zone ${state.direction.replace("-", " → Zone ")}`;
  document.getElementById("predDirection").textContent = dirText;
  document.getElementById("predNextZone").textContent = zoneNames[state.predicted] || "None";

  for (let i = 0; i < 3; i++) {
    const pill = document.getElementById("predIr" + (i + 1));
    pill.textContent = state.ir[i] ? "Detected" : "Clear";
    pill.classList.toggle("pill-detected", state.ir[i]);
    pill.classList.toggle("pill-clear", !state.ir[i]);
    document.getElementById("predBright" + (i + 1)).textContent = state.led[i] + "%";
  }

  // Road visualization
  for (let i = 0; i < 3; i++) {
    const n = i + 1;
    const glow = document.getElementById("glow" + n);
    const lamp = document.getElementById("lamp" + n);
    const brightness = state.led[i] / 100;
    glow.setAttribute("opacity", (0.06 + brightness * 0.55).toFixed(2));
    glow.setAttribute("r", 30 + brightness * 26);
    lamp.setAttribute("fill", brightness > 0.05 ? "var(--amber)" : "var(--post)");
  }
}

function renderTrafficPage() {
  document.getElementById("trafficLevelBadge").textContent = state.traffic;
  const activeCount = state.ir.filter(v => v).length;
  document.getElementById("trafficActiveCount").textContent = `${activeCount} / 3`;
  document.getElementById("trafficDetections").textContent = state.detections;

  const baselineMap = { LOW: 20, MEDIUM: 50, HIGH: 100 };
  document.getElementById("trafficBaseline").textContent = (baselineMap[state.traffic] ?? 20) + "%";

  for (let i = 0; i < 3; i++) {
    document.getElementById("trafficLed" + (i + 1)).textContent = state.led[i] + "%";
  }

  // Append to rolling history (only on level, sampled per telemetry frame)
  trafficHistory.push(state.traffic);
  if (trafficHistory.length > HISTORY_LENGTH) trafficHistory.shift();
  renderHistoryStrip();
}

function renderHistoryStrip() {
  const strip = document.getElementById("historyStrip");
  strip.innerHTML = "";
  for (const level of trafficHistory) {
    const bar = document.createElement("div");
    bar.className = "history-bar " + level.toLowerCase();
    strip.appendChild(bar);
  }
}

function renderEmergencyPage() {
  const banner = document.getElementById("emergencyStatusBanner");
  banner.textContent = state.emergency ? "EMERGENCY ACTIVE" : "AUTOMATIC MODE";
  banner.classList.toggle("active", state.emergency);

  for (let i = 0; i < 3; i++) {
    document.getElementById("emLed" + (i + 1)).textContent = state.led[i] + "%";
  }
}

function renderWeatherPage() {
  const nameMap = { CLEAR: "Clear", RAIN: "Normal rain", HEAVY: "Heavy rain", FOG: "Fog" };
  const targetMap = { CLEAR: "20%", RAIN: "50%", HEAVY: "100%", FOG: "100%" };

  document.getElementById("weatherCurrent").textContent = nameMap[state.weather] || state.weather;
  document.getElementById("weatherTarget").textContent = targetMap[state.weather] || "—";
  document.getElementById("weatherModeStatus").textContent = state.emergency ? "Overridden by emergency mode" : "Active";

  document.querySelectorAll(".weather-option").forEach(btn => {
    btn.classList.toggle("active", btn.dataset.weather === state.weather);
  });
}

// ---------- Navigation ----------

document.querySelectorAll(".tab").forEach(tab => {
  tab.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach(t => { t.classList.remove("active"); t.setAttribute("aria-selected", "false"); });
    tab.classList.add("active");
    tab.setAttribute("aria-selected", "true");

    document.querySelectorAll(".page").forEach(p => p.classList.remove("active"));
    document.getElementById("page-" + tab.dataset.page).classList.add("active");
  });
});

// ---------- Controls ----------

document.getElementById("quickEmergencyBtn").addEventListener("click", () => {
  sendCommand(state.emergency ? "EMERGENCY:OFF" : "EMERGENCY:ON");
});

document.getElementById("emergencyOnBtn").addEventListener("click", () => {
  sendCommand("EMERGENCY:ON");
});

document.getElementById("emergencyOffBtn").addEventListener("click", () => {
  sendCommand("EMERGENCY:OFF");
});

document.querySelectorAll(".weather-option").forEach(btn => {
  btn.addEventListener("click", () => {
    sendCommand("WEATHER:" + btn.dataset.weather);
  });
});

// Initial paint (all placeholders until first telemetry frame arrives)
renderAll();
