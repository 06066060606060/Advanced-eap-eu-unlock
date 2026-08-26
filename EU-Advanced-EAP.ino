// T2CAN Unified - Dual CAN (MCP2515 + TWAI) for LilyGo T-2CAN
//
// Standard model:
//   CAN A (MCP2515) -> Nag Echo  (ID 0x370)
//   CAN B (TWAI)    -> Summon Unlock (IDs 280, 390, 921, 1016, 1021)
//
// Model YL (set MODEL_YL to 1 below):
//   CAN A (MCP2515) -> Nag Echo + summon STATUS frames 280 / 390 / 921 / 
//   CAN B (TWAI)    -> Summon injection only, on 1021 & 1016

// ── Model selection ──
// 1 = Model YL : summon status frames (280/390/921) are read on CAN A
//     (MCP2515), alongside the nag-killer. CAN B (TWAI) only carries 1021 & 1016.
// 0 = Standard : all summon frames (status + 1021) are on CAN B (TWAI).
#define MODEL_YL 1

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <Update.h>
#include "driver/twai.h"
#include "index_html.h"

#define FW_VERSION "T2CAN-v2.3 rev.18"

// T-2CAN board specific
#include "pin_config.h"
#include <mcp2515.h>
#include <SPI.h>

struct NagConfig;
struct NagContext;

static unsigned long bootTime = 0;
static unsigned long canInitTime = 0;
static volatile bool twaiReady = false;
static volatile bool mcpReady = false;
static volatile uint32_t canAnyFrames = 0;
static volatile unsigned long lastCanFrameMs = 0;
static volatile uint32_t canBeat = 0;
static volatile uint32_t canRxBeat = 0;
static volatile uint32_t webBeat = 0;
static volatile uint32_t runtimeStatsResetCount = 0;
static volatile uint32_t runtimeStatsLastResetMs = 0;
RTC_DATA_ATTR uint32_t rtcBootCount = 0;
static Preferences prefs;

// ═══════════════════════════════════════════════════════════════
// BOOT / FIRST-CAN TIMING CAPTURE (rev.15)
//
// Passive diagnostics only. These timestamps never participate in feature
// gating, CAN recovery decisions, or TX/injection. Capture starts
// automatically on every ESP32 boot and can be exported as CSV from:
//   /api/system/boot-capture.csv
// All timestamps are milliseconds from setup() entry (bootTime).
// ═══════════════════════════════════════════════════════════════

static constexpr uint32_t BOOT_CAPTURE_UNSET = 0xFFFFFFFFUL;
static portMUX_TYPE bootCaptureMux = portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t bootCapCanInitDoneMs = BOOT_CAPTURE_UNSET;
static volatile uint32_t bootCapCanTasksStartedMs = BOOT_CAPTURE_UNSET;
static volatile uint32_t bootCapWifiReadyMs = BOOT_CAPTURE_UNSET;
static volatile uint32_t bootCapFirstCanAMs = BOOT_CAPTURE_UNSET;
static volatile uint32_t bootCapFirstCanBMs = BOOT_CAPTURE_UNSET;
static volatile uint32_t bootCapFirst370Ms = BOOT_CAPTURE_UNSET;
static volatile uint32_t bootCapFirst370TorqueMs = BOOT_CAPTURE_UNSET;
static volatile uint32_t bootCapFirst399Ms = BOOT_CAPTURE_UNSET;
static volatile uint32_t bootCapFirstParty24AMs = BOOT_CAPTURE_UNSET;
static volatile uint32_t bootCapFirstVh249Ms = BOOT_CAPTURE_UNSET;
static volatile uint16_t bootCapFirst370Raw = 0xFFFF;
static volatile uint16_t bootCapFirst370TorqueRaw = 0xFFFF;

struct BootHardReinitEvent {
  uint32_t startMs;
  uint32_t endMs;
  uint8_t reason;
  int8_t success; // -1=in progress, 0=failed, 1=success
};

static constexpr uint8_t BOOT_CAPTURE_HARD_MAX = 8;
static BootHardReinitEvent bootCapHard[BOOT_CAPTURE_HARD_MAX] = {};
static volatile uint8_t bootCapHardCount = 0;
static volatile uint32_t bootCapHardDropped = 0;

static inline uint32_t bootCaptureNowMs() {
  return (uint32_t)(millis() - bootTime);
}

static void bootCaptureMarkOnce(volatile uint32_t *slot) {
  // Fast path after the first event: no critical section on normal CAN traffic.
  if (*slot != BOOT_CAPTURE_UNSET) return;
  const uint32_t t = bootCaptureNowMs();
  portENTER_CRITICAL(&bootCaptureMux);
  if (*slot == BOOT_CAPTURE_UNSET) *slot = t;
  portEXIT_CRITICAL(&bootCaptureMux);
}

static void bootCaptureObservePartyFrame(uint16_t id, uint8_t dlc, const uint8_t *data) {
  bootCaptureMarkOnce(&bootCapFirstCanAMs);

  if (id == 0x399 && dlc >= 1)
    bootCaptureMarkOnce(&bootCapFirst399Ms);

  if (id == 0x24A && dlc >= 8)
    bootCaptureMarkOnce(&bootCapFirstParty24AMs);

  if (id == 0x370 && dlc >= 4 &&
      (bootCapFirst370Ms == BOOT_CAPTURE_UNSET ||
       bootCapFirst370TorqueMs == BOOT_CAPTURE_UNSET)) {
    const uint16_t raw = (uint16_t)(((data[2] & 0x0F) << 8) | data[3]);
    const uint32_t t = bootCaptureNowMs();
    const int32_t d = (int32_t)raw - 2050;
    portENTER_CRITICAL(&bootCaptureMux);
    if (bootCapFirst370Ms == BOOT_CAPTURE_UNSET) {
      bootCapFirst370Ms = t;
      bootCapFirst370Raw = raw;
    }
    // TORQUE (REAL) uses raw*0.01 - 20.5, so raw=2050 is 0.00 Nm.
    // Record the first frame at |torque| >= 0.10 Nm to distinguish
    // "0x370 arrived" from "a visibly non-zero torque arrived".
    if (bootCapFirst370TorqueMs == BOOT_CAPTURE_UNSET && (d >= 10 || d <= -10)) {
      bootCapFirst370TorqueMs = t;
      bootCapFirst370TorqueRaw = raw;
    }
    portEXIT_CRITICAL(&bootCaptureMux);
  }
}

static void bootCaptureObserveVhFrame(uint32_t id, uint8_t dlc) {
  bootCaptureMarkOnce(&bootCapFirstCanBMs);
  if (id == 0x249 && dlc >= 4)
    bootCaptureMarkOnce(&bootCapFirstVh249Ms);
}

static int8_t bootCaptureHardStart(uint8_t reason) {
  const uint32_t t = bootCaptureNowMs();
  int8_t idx = -1;
  portENTER_CRITICAL(&bootCaptureMux);
  if (bootCapHardCount < BOOT_CAPTURE_HARD_MAX) {
    idx = (int8_t)bootCapHardCount++;
    bootCapHard[idx].startMs = t;
    bootCapHard[idx].endMs = BOOT_CAPTURE_UNSET;
    bootCapHard[idx].reason = reason;
    bootCapHard[idx].success = -1;
  } else {
    bootCapHardDropped++;
  }
  portEXIT_CRITICAL(&bootCaptureMux);
  return idx;
}

static void bootCaptureHardFinish(int8_t idx, bool success) {
  if (idx < 0 || idx >= (int8_t)BOOT_CAPTURE_HARD_MAX) return;
  const uint32_t t = bootCaptureNowMs();
  portENTER_CRITICAL(&bootCaptureMux);
  bootCapHard[(uint8_t)idx].endMs = t;
  bootCapHard[(uint8_t)idx].success = success ? 1 : 0;
  portEXIT_CRITICAL(&bootCaptureMux);
}

static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXTERNAL_RESET";
    case ESP_RST_SW:        return "SOFTWARE_RESET";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

// ═══════════════════════════════════════════════════════════════
// MCP2515 GLOBALS
// ═══════════════════════════════════════════════════════════════

static constexpr CAN_CLOCK MCP_CLOCK = MCP_16MHZ;

static constexpr uint32_t MCP_SPI_HZ = 10000000;

static constexpr uint8_t MCP_RX_BUDGET = 32;

static MCP2515 Can_A(MCP2515_CS, MCP_SPI_HZ, &SPI);
static volatile uint8_t  mcpState = 0;      // 0=OK, 1=WARN, 2=BUS-OFF
static volatile uint32_t mcpTxOk = 0;
static volatile uint32_t mcpTxFail = 0;
static volatile uint8_t  mcpTxFailConsecutive = 0;
static volatile uint32_t mcpRxCount = 0;
static unsigned long lastMcpStatusMs = 0;
static unsigned long lastMcpRecoverMs = 0;


// ═══════════════════════════════════════════════════════════════
// CAN RECOVERY SUPERVISOR (ported from V2.14 recovery architecture)
//
// IMPORTANT: this block does NOT participate in NAG / Summon / TLSSC / Advanced EAP /
// Auto Blinker gating. Original V2.3 feature logic remains authoritative.
// It only monitors CAN controller/task liveness and recreates the CAN
// subsystem when acquisition or wake recovery fails.
// ═══════════════════════════════════════════════════════════════

enum CanSupervisorCommand : uint8_t {
  CAN_SUP_NONE = 0,
  CAN_SUP_HARD_ACQUIRE = 1,
  CAN_SUP_HARD_STALE = 2,
  CAN_SUP_HARD_MANUAL = 3
};

static portMUX_TYPE canRecoveryMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t canSupervisorCommand = CAN_SUP_NONE;
static volatile bool canSubsystemBusy = false;
static volatile bool canTasksStopping = false;
static volatile bool canTaskMcpQuiesced = false;
static volatile bool canTaskTwaiQuiesced = false;
static TaskHandle_t canTaskMcpHandle = nullptr;
static TaskHandle_t canTaskTwaiHandle = nullptr;
static TaskHandle_t canSupervisorHandle = nullptr;

static volatile uint32_t canTaskMcpHeartbeatMs = 0;
static volatile uint32_t canTaskTwaiHeartbeatMs = 0;
static volatile uint32_t lastCanAFrameMs = 0;
static volatile uint32_t lastCanBFrameMs = 0;
static volatile uint32_t canHardReinitCount = 0;
static volatile uint32_t canHardReinitFailCount = 0;
static volatile uint8_t  canLastHardReinitReason = CAN_SUP_NONE;
static volatile uint32_t canRecoverySleepCount = 0;
static volatile uint32_t canRecoveryWakeCount = 0;

static bool mcpSpiStarted = false;
static bool recoveryEverBothActive = false;
static bool recoverySleeping = false;
static uint32_t recoveryWakeAcquireStartMs = 0;
static uint32_t recoveryOneBusStaleStartMs = 0;
static uint32_t recoveryLastHardRequestMs = 0;
static uint32_t recoveryLastBothActiveMs = 0;
static uint8_t recoveryColdRetryCount = 0;
static bool recoveryColdRetriesExhausted = false;

static constexpr uint32_t RECOVERY_BUS_FRESH_MS = 2000;
static constexpr uint32_t RECOVERY_SLEEP_QUIET_MS = 5000;
static constexpr uint32_t RECOVERY_WAKE_ACQUIRE_MS = 5000;
static constexpr uint32_t RECOVERY_ONE_BUS_STALE_MS = 4000;
// rev.14: fast cold acquisition; CAN RX starts as soon as controllers are ready.
// Keep task-heartbeat startup grace separate so fast acquisition does not make
// the task watchdog unnecessarily aggressive.
static constexpr uint32_t RECOVERY_COLD_FIRST_ACQUIRE_MS = 2000;
static constexpr uint32_t RECOVERY_TASK_START_GRACE_MS = 5000;
static constexpr uint32_t RECOVERY_HARD_COOLDOWN_MS = 10000;
static constexpr uint32_t RECOVERY_COLD_RETRY_INTERVAL_MS = 15000;
static constexpr uint8_t  RECOVERY_COLD_MAX_RETRIES = 3;
static constexpr uint32_t RECOVERY_TASK_HEARTBEAT_TIMEOUT_MS = 3000;
static constexpr uint32_t RECOVERY_TASK_STOP_SETTLE_MS = 50;
static constexpr uint32_t RECOVERY_TWAI_WAIT_MS = 1800;

static void requestCanSubsystemRestart(uint8_t reason);

// ═══════════════════════════════════════════════════════════════
// NAG ECHO (CAN A - MCP2515)
// ═══════════════════════════════════════════════════════════════

static const uint16_t NAG_TORQUE_RAW_MAX = 0x8B6;
static const uint16_t NAG_TORQUE_RAW_MIN = 0x74E;
static const float    NAG_TORQUE_NM_MAX  = +1.80f;
static const float    NAG_TORQUE_NM_MIN  = -1.80f;
static const uint8_t  NAG_MAX_TORQUE_ENTRIES = 8;
static const unsigned long NAG_INJECTION_DELAY_MS = 15000;

enum NagMode : uint8_t { MODE_A = 0, MODE_B = 1, MODE_CUSTOM = 2};

struct NagConfig {
  bool     enabled;
  uint8_t  mode;
  uint16_t targetId;
  uint8_t  torqueCount;
  uint8_t  torqueB2[NAG_MAX_TORQUE_ENTRIES];
  uint8_t  torqueB3[NAG_MAX_TORQUE_ENTRIES];
  uint8_t  hoRatePct;
  uint16_t burstMs;
  uint16_t pauseMs;
  uint16_t apStateId;
  uint8_t  apStateByte;
  uint8_t  apStateShift;
  uint8_t  apStateMask;
  uint8_t  handsOnByte;
  uint8_t  handsOnShift;
  uint8_t  handsOnMask;
  uint16_t steeringId;
  uint8_t  steeringByteHi;
  uint8_t  steeringByteLo;
  float    steeringScale;
  float    steeringOffset;
};

static NagConfig nagCfg;
static portMUX_TYPE nagCfgMux = portMUX_INITIALIZER_UNLOCKED;

struct NagContext {
  uint8_t  apState;
  uint8_t  handsOnState;
  uint8_t  prevHandsOnState;
  float    steeringAngleDeg;
  unsigned long lastApStateMs;
  unsigned long lastSteeringMs;
  unsigned long state2EnterMs;
  unsigned long state3EnterMs;
  uint16_t walkSeed;
  float    lastModeCTorqueNm;
};

static NagContext nagCtx;
static portMUX_TYPE nagCtxMux = portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t nagRxFrames    = 0;
static volatile uint32_t nagEchoCount   = 0;
static volatile uint32_t nagTxOk        = 0;
static volatile uint32_t nagTxFail      = 0;
static volatile uint32_t nagEchoLatUs   = 0;
static volatile uint8_t  nagRealHo      = 0;
static volatile float    nagRealTorque  = 0;
static volatile uint8_t  nagLastInjectedHo = 0;
static volatile float    nagLastInjectedNm = 0;
static unsigned long nagLastTxFailLog = 0;
// rev.08: Mode B burst/pause timing starts from AP engagement rather than boot uptime.
// This is intentionally independent of the Original mcpRxCount > 1000 warmup check.
static volatile uint32_t nagModeBPhaseStartMs = 0;

// ── Nag helpers ──

static void nagClampTorque(uint8_t& b2, uint8_t& b3) {
  uint16_t raw = ((b2 & 0x0F) << 8) | b3;
  if (raw > NAG_TORQUE_RAW_MAX) raw = NAG_TORQUE_RAW_MAX;
  if (raw < NAG_TORQUE_RAW_MIN) raw = NAG_TORQUE_RAW_MIN;
  b2 = (b2 & 0xF0) | ((raw >> 8) & 0x0F);
  b3 = raw & 0xFF;
}

static void nagNmToBytes(float nm, uint8_t& b2lo, uint8_t& b3) {
  if (nm > NAG_TORQUE_NM_MAX) nm = NAG_TORQUE_NM_MAX;
  if (nm < NAG_TORQUE_NM_MIN) nm = NAG_TORQUE_NM_MIN;
  uint16_t raw = (uint16_t)((nm + 20.5f) * 100.0f + 0.5f);
  if (raw > NAG_TORQUE_RAW_MAX) raw = NAG_TORQUE_RAW_MAX;
  if (raw < NAG_TORQUE_RAW_MIN) raw = NAG_TORQUE_RAW_MIN;
  b2lo = (raw >> 8) & 0x0F;
  b3   = raw & 0xFF;
}

static void nagCfgSetCommonDefaults(NagConfig& c) {
  c.enabled        = true;
  c.burstMs        = 1000;
  c.pauseMs        = 1500;
  c.apStateId      = 0x399;
  c.apStateByte    = 0;
  c.apStateShift   = 4;
  c.apStateMask    = 0x0F;
  c.handsOnByte    = 0;
  c.handsOnShift   = 0;
  c.handsOnMask    = 0x0F;
  c.steeringId     = 0x129;
  c.steeringByteHi = 1;
  c.steeringByteLo = 0;
  c.steeringScale  = 0.1f;
  c.steeringOffset = 0.0f;
}

static void nagCfgDefaultsModeA(NagConfig& c) {
  nagCfgSetCommonDefaults(c);
  c.mode        = MODE_A;
  c.targetId    = 0x370;
  c.torqueCount = 1;
  c.torqueB2[0] = 0x08;
  c.torqueB3[0] = 0xB6;
  c.hoRatePct   = 100;
}
static void nagCfgDefaultsModeB(NagConfig& c) {
  nagCfgSetCommonDefaults(c);
  c.mode        = MODE_B;
  c.targetId    = 0x370;
  c.torqueCount = 4;
  c.torqueB2[0] = 0x08; c.torqueB3[0] = 0xB6;
  c.torqueB2[1] = 0x08; c.torqueB3[1] = 0x98;
  c.torqueB2[2] = 0x07; c.torqueB3[2] = 0x6C;
  c.torqueB2[3] = 0x07; c.torqueB3[3] = 0x4E;
  c.hoRatePct   = 100;
}


static void nagCfgClampAll(NagConfig& c) {
  if (c.torqueCount < 1) c.torqueCount = 1;
  if (c.torqueCount > NAG_MAX_TORQUE_ENTRIES) c.torqueCount = NAG_MAX_TORQUE_ENTRIES;
  if (c.hoRatePct > 100) c.hoRatePct = 100;
  if (c.burstMs < 50)    c.burstMs   = 50;
  if (c.burstMs > 10000) c.burstMs   = 10000;
  if (c.pauseMs > 10000) c.pauseMs   = 10000;
  for (uint8_t i = 0; i < c.torqueCount; i++) nagClampTorque(c.torqueB2[i], c.torqueB3[i]);
}

static void nagCfgLoad() {
  Serial.println("NVS: Loading nag config...");
  if (!prefs.begin("nag", true)) {
    Serial.println("NVS: No existing nag config, using defaults");
    nagCfgDefaultsModeA(nagCfg);
    return;
  }
  if (!prefs.isKey("v")) {
    prefs.end();
    nagCfgDefaultsModeA(nagCfg);
    return;
  }
  nagCfgSetCommonDefaults(nagCfg);
  nagCfg.enabled        = prefs.getBool("en", true);
  nagCfg.mode           = prefs.getUChar("mode", 0);
  nagCfg.targetId       = prefs.getUShort("id", 0x370);
  nagCfg.torqueCount    = prefs.getUChar("tc", 1);
  size_t n = prefs.getBytes("tb2", nagCfg.torqueB2, NAG_MAX_TORQUE_ENTRIES);
  if (n == 0) { nagCfg.torqueB2[0] = 0x08; }
  n = prefs.getBytes("tb3", nagCfg.torqueB3, NAG_MAX_TORQUE_ENTRIES);
  if (n == 0) { nagCfg.torqueB3[0] = 0xB6; }
  nagCfg.hoRatePct      = prefs.getUChar("ho", 100);
  nagCfg.burstMs        = prefs.getUShort("bms", 1000);
  nagCfg.pauseMs        = prefs.getUShort("pms", 1500);
  nagCfg.apStateId      = prefs.getUShort("apid", 0x399);
  nagCfg.steeringId     = prefs.getUShort("stid", 0x129);
  prefs.end();
  nagCfgClampAll(nagCfg);
  Serial.println("NVS: Nag config loaded OK");
}

static void nagCfgSave() {
  nagCfgClampAll(nagCfg);
  if (!prefs.begin("nag", false)) {
    Serial.println("NVS: Nag save failed - could not open");
    return;
  }
  prefs.putBool("en",     nagCfg.enabled);
  prefs.putUChar("mode",  nagCfg.mode);
  prefs.putUShort("id",   nagCfg.targetId);
  prefs.putUChar("tc",    nagCfg.torqueCount);
  prefs.putBytes("tb2",   nagCfg.torqueB2, NAG_MAX_TORQUE_ENTRIES);
  prefs.putBytes("tb3",   nagCfg.torqueB3, NAG_MAX_TORQUE_ENTRIES);
  prefs.putUChar("ho",    nagCfg.hoRatePct);
  prefs.putUShort("bms",  nagCfg.burstMs);
  prefs.putUShort("pms",  nagCfg.pauseMs);
  prefs.putUShort("apid", nagCfg.apStateId);
  prefs.putUShort("stid", nagCfg.steeringId);
  prefs.putUChar("v",     2);
  prefs.end();
}

// ── Nag decide injection (raw data version) ──

static bool nagDecideInjection(uint8_t dlc,
                            uint8_t& out_b2, uint8_t& out_b3, bool& out_setHo) {
  if (dlc < 8) return false;
  unsigned long now = millis();

  uint8_t  mode, tCount, hoPct;
  uint16_t burstMs, pauseMs;
  uint8_t  tB2[NAG_MAX_TORQUE_ENTRIES], tB3[NAG_MAX_TORQUE_ENTRIES];

  portENTER_CRITICAL(&nagCfgMux);
  mode    = nagCfg.mode;
  tCount  = nagCfg.torqueCount;
  hoPct   = nagCfg.hoRatePct;
  burstMs = nagCfg.burstMs;
  pauseMs = nagCfg.pauseMs;
  for (uint8_t i = 0; i < tCount; i++) {
    tB2[i] = nagCfg.torqueB2[i];
    tB3[i] = nagCfg.torqueB3[i];
  }
  portEXIT_CRITICAL(&nagCfgMux);

  static uint8_t  tIdx = 0;
  static uint16_t hoSeq = 0;
  static uint32_t lastChangeMs = 0;
  static uint8_t  prevMode = 0xFF;

  if (mode != prevMode) {
    tIdx = 0; hoSeq = 0; lastChangeMs = now; prevMode = mode;
    if (mode == MODE_B) nagModeBPhaseStartMs = now;
  }

  if (mode == MODE_A || mode == MODE_CUSTOM) {
    out_b2 = tB2[tIdx % tCount];
    out_b3 = tB3[tIdx % tCount];
    tIdx++;
    bool setHo = ((hoSeq * 100u) / 65536u < (uint16_t)hoPct);
    hoSeq = (uint16_t)(hoSeq * 1103u + 12345u);
    out_setHo = setHo;
    return true;
  }

  if (mode == MODE_B) {
    uint32_t cycleMs = (uint32_t)burstMs + (uint32_t)pauseMs;
    if (cycleMs == 0) cycleMs = 1;
    uint32_t phaseStart = nagModeBPhaseStartMs;
    if (phaseStart == 0) {
      phaseStart = now;
      nagModeBPhaseStartMs = now;
    }
    uint32_t phase = (uint32_t)(now - phaseStart) % cycleMs;
    if (phase >= burstMs) return false;
    if (now - lastChangeMs >= 200) { tIdx = (tIdx + 1) % tCount; lastChangeMs = now; }
    out_b2 = tB2[tIdx];
    out_b3 = tB3[tIdx];
    out_setHo = true;
    return true;
  }

  return false;
}

// ── Nag context updates (raw data) ──

static void nagUpdateApState(const uint8_t* data, uint8_t dlc) {
  if (dlc < 8) return;
  uint8_t apb, apsh, apmask, hob, hosh, homask;
  portENTER_CRITICAL(&nagCfgMux);
  apb = nagCfg.apStateByte; apsh = nagCfg.apStateShift; apmask = nagCfg.apStateMask;
  hob = nagCfg.handsOnByte; hosh = nagCfg.handsOnShift; homask = nagCfg.handsOnMask;
  portEXIT_CRITICAL(&nagCfgMux);
  if (apb >= dlc || hob >= dlc) return;
  uint8_t ap = (data[apb] >> apsh) & apmask;
  uint8_t ho = (data[hob] >> hosh) & homask;
  unsigned long now = millis();
  portENTER_CRITICAL(&nagCtxMux);
  nagCtx.apState = ap;
  nagCtx.lastApStateMs = now;
  if (ho != nagCtx.handsOnState) {
    nagCtx.prevHandsOnState = nagCtx.handsOnState;
    nagCtx.handsOnState = ho;
    if (ho == 2 && nagCtx.state2EnterMs == 0) nagCtx.state2EnterMs = now;
    if (ho != 2) nagCtx.state2EnterMs = 0;
    if (ho == 3 && nagCtx.state3EnterMs == 0) nagCtx.state3EnterMs = now;
    if (ho != 3) nagCtx.state3EnterMs = 0;
  }
  portEXIT_CRITICAL(&nagCtxMux);
}

static void nagUpdateSteering(const uint8_t* data, uint8_t dlc) {
  if (dlc < 8) return;
  uint8_t bh, bl; float scale, offs;
  portENTER_CRITICAL(&nagCfgMux);
  bh = nagCfg.steeringByteHi; bl = nagCfg.steeringByteLo;
  scale = nagCfg.steeringScale; offs = nagCfg.steeringOffset;
  portEXIT_CRITICAL(&nagCfgMux);
  if (bh >= dlc || bl >= dlc) return;
  int16_t raw = (int16_t)(((uint16_t)data[bh] << 8) | data[bl]);
  float deg = raw * scale + offs;
  unsigned long now = millis();
  portENTER_CRITICAL(&nagCtxMux);
  nagCtx.steeringAngleDeg = deg;
  nagCtx.lastSteeringMs = now;
  portEXIT_CRITICAL(&nagCtxMux);
}

// ── Forward declarations : summon status handlers (defined in CAN B section) ──
// On Model YL these are also called from CAN A (MCP2515).
static void handle280(const uint8_t *data);
static void handle390(const uint8_t *data);
static void handle921(const uint8_t *data);
static void handle1016(const uint8_t *data, uint8_t dlc);
static bool nagApInjectionGateOpen();

// ── Nag process frame from MCP2515 ──

static void nagProcessMcpFrame(const struct can_frame& rxf) {
  uint16_t id = rxf.can_id & 0x7FF;
  uint8_t dlc = rxf.can_dlc;
  if (dlc < 1) return;

#if MODEL_YL
  // Model YL : summon STATUS frames 280/390/921 are read on CAN A (MCP2515).
  // 1016 (SPR) and the 1021 injection stay on CAN B (TWAI).
  switch (id) {
    case 280:
      if (dlc >= 7) handle280(rxf.data);
      break;
    case 390:
      if (dlc >= 8) handle390(rxf.data);
      break;
    case 921:
      if (dlc >= 1) handle921(rxf.data);
      break;
    default:
      break;
  }
#endif

  uint16_t targetId, apStateId, steeringId;
  bool en;
  portENTER_CRITICAL(&nagCfgMux);
  targetId   = nagCfg.targetId;
  apStateId  = nagCfg.apStateId;
  steeringId = nagCfg.steeringId;
  en         = nagCfg.enabled;
  portEXIT_CRITICAL(&nagCfgMux);

  if (id == apStateId)  nagUpdateApState(rxf.data, dlc);
  if (id == steeringId) nagUpdateSteering(rxf.data, dlc);

  if (id != targetId) return;
  nagRxFrames++;

  if (dlc < 5) return;
  uint8_t ho = (rxf.data[4] >> 6) & 0x03;
  uint16_t tRaw = ((rxf.data[2] & 0x0F) << 8) | rxf.data[3];
  nagRealHo     = ho;
  nagRealTorque = tRaw * 0.01f - 20.5f;

  bool isOurs = false;
  if (ho == 1) {
    portENTER_CRITICAL(&nagCfgMux);
    for (uint8_t i = 0; i < nagCfg.torqueCount; i++) {
      uint16_t cfgRaw = ((nagCfg.torqueB2[i] & 0x0F) << 8) | nagCfg.torqueB3[i];
      if (tRaw == cfgRaw) { isOurs = true; break; }
    }
    portEXIT_CRITICAL(&nagCfgMux);
  }

  bool bootDelayPassed = (millis() - canInitTime) >= NAG_INJECTION_DELAY_MS;
  bool canSeen = mcpRxCount > 1000;  // Original warmup counter retained in rev.08.
  bool apActiveForNag = nagApInjectionGateOpen();
  if (en && apActiveForNag && bootDelayPassed && canSeen && !isOurs && ho <= 1) {
    uint8_t b2 = 0, b3 = 0; bool setHo = false;
    if (nagDecideInjection(dlc, b2, b3, setHo)) {
      struct can_frame txf;
      txf.can_id = rxf.can_id;
      txf.can_dlc = rxf.can_dlc;
      memcpy(txf.data, rxf.data, 8);
      txf.data[2] = (txf.data[2] & 0xF0) | (b2 & 0x0F);
      txf.data[3] = b3;
      txf.data[4] = setHo ? (txf.data[4] | 0x40) : txf.data[4];
      txf.data[6] = (txf.data[6] & 0xF0) | (((txf.data[6] & 0x0F) + 1) & 0x0F);
      uint16_t s = txf.data[0] + txf.data[1] + txf.data[2] + txf.data[3]
                 + txf.data[4] + txf.data[5] + txf.data[6];
      txf.data[7] = (uint8_t)((s + 0x73) & 0xFF);

      unsigned long t0 = micros();
      MCP2515::ERROR err = Can_A.sendMessage(&txf);
      nagEchoLatUs = micros() - t0;

      if (err == MCP2515::ERROR_OK) {
        mcpTxOk++; nagEchoCount++;
        mcpTxFailConsecutive = 0;
        nagLastInjectedHo = setHo ? 1 : 0;
        uint16_t raw = ((b2 & 0x0F) << 8) | b3;
        nagLastInjectedNm = raw * 0.01f - 20.5f;
      } else {
        mcpTxFail++;
        if (mcpTxFailConsecutive < 255) mcpTxFailConsecutive++;
        unsigned long now = millis();
        if (now - nagLastTxFailLog >= 2000) {
          nagLastTxFailLog = now;
          Serial.printf("[NAG TX FAIL] MCP err=%d total=%lu\n", (int)err, (unsigned long)mcpTxFail);
        }
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// SUMMON UNLOCK (CAN B - TWAI)
// ═══════════════════════════════════════════════════════════════

static inline uint8_t readMuxID(const uint8_t *data) {
    return data[0] & 0x07;
}
static inline bool getBit(const uint8_t *data, int bit) {
    return (data[bit / 8] >> (bit % 8)) & 0x01;
}
static inline void setBit(uint8_t *data, int bit, bool val) {
    uint8_t mask = (uint8_t)(1U << (bit % 8));
    if (val) data[bit / 8] |=  mask;
    else     data[bit / 8] &= ~mask;
}
static inline uint8_t readDIGear(const uint8_t *data) {
    return (data[2] >> 5) & 0x07;
}
static inline uint8_t readVehicleGear(const uint8_t *data) {
    return (data[2] >> 5) & 0x07;
}
static inline int gearState(uint8_t gear) {
    if (gear == 1)             return  1;
    if (gear == 2 || gear == 3 || gear == 4) return 0;
    return -1;
}
static inline uint8_t readDASStatus(const uint8_t *data) {
    return data[0] & 0x07;
}
// Model YL NOA discriminator. Keep the legacy 3-bit AP decoder above
// unchanged for NAG and the existing AP gate; Auto Blinker alone uses the
// full low nibble so ACTIVE_NAV (5) can be gated independently.
static inline uint8_t readDASState4(const uint8_t *data) {
    return data[0] & 0x0F;
}

static volatile bool forceMode = false;
static portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool summonEnabled = true;
static volatile bool tlsscEnabled  = false;   // "Enable TLSSC" - off by default
static volatile bool gateAPActive  = false;
static volatile bool gateNOAActive = false;   // raw DAS_autopilotState == ACTIVE_NAV (5)
static volatile uint8_t dasAutopilotState4 = 0xFF; // low nibble of Party-CAN 0x399 byte0
static volatile uint32_t lastDASStatusMillis = 0;  // last valid Party-CAN 0x399 RX
static constexpr uint32_t NOA_STATUS_FRESH_MS = 2000; // fail-closed Auto Blinker gate

// Auto Blinker sees NOA as valid only while the latest 0x399 status is fresh.
// The legacy AP/NAG gate intentionally remains unchanged.
static bool autoBlinkerNOAGateOpen(uint32_t now, uint32_t* ageOut = nullptr) {
  bool rawNoa;
  uint32_t last;
  portENTER_CRITICAL(&stateMux);
  rawNoa = gateNOAActive;
  last = lastDASStatusMillis;
  portEXIT_CRITICAL(&stateMux);

  uint32_t age = (last == 0) ? UINT32_MAX : (uint32_t)(now - last);
  if (ageOut) *ageOut = age;
  return rawNoa && last != 0 && age <= NOA_STATUS_FRESH_MS;
}
static volatile bool gateParked    = true;
static volatile bool gateSummoning = false;
static volatile bool sprSeen  = false;
static volatile bool lastAca  = false;
#define PARKED_TIMEOUT_MS  5000
static volatile uint32_t last280Millis = 0;

// Summon TX priority is intentionally separate from the Original feature gate.
// It never writes gateParked/gateSummoning/lastAca/sprSeen/forceMode.
// NORMAL        : driving / no fresh confirmed Park. No priority shedding/flush.
// PARK_STANDBY  : fresh Park confirmed. Reserve queue headroom for a Summon start.
// SUMMON_FULL   : Summon session confirmed. Summon owns CAN B TX priority.
enum SummonPriorityState : uint8_t {
  SUMMON_PRIORITY_NORMAL = 0,
  SUMMON_PRIORITY_PARK_STANDBY = 1,
  SUMMON_PRIORITY_FULL = 2
};
static volatile uint8_t summonPriorityState = SUMMON_PRIORITY_NORMAL;
static volatile int8_t priorityGear280State = -1;
static volatile int8_t priorityGear390State = -1;
static volatile uint32_t priorityGear280Ms = 0;
static volatile uint32_t priorityGear390Ms = 0;
static volatile uint32_t summonPriorityStateSinceMs = 0;
static volatile uint32_t summonPriorityTransitions = 0;
static volatile uint32_t summonPriorityFullEnterCount = 0;
static volatile uint32_t summonPriorityFullExitCount = 0;
static volatile uint32_t summonPriorityFullInactiveSinceMs = 0;
static constexpr uint32_t SUMMON_PRIORITY_PARK_FRESH_MS = 3000;
static constexpr uint32_t SUMMON_PRIORITY_FULL_EXIT_GRACE_MS = 1500;

static volatile uint32_t sumRxMux1   = 0;
static volatile uint32_t sumTxOk     = 0;
static volatile uint32_t sumTxFail   = 0;
static volatile uint32_t sumRx280    = 0;
static volatile uint32_t sumRx390    = 0;
static volatile uint32_t sumRx921    = 0;
static volatile uint32_t sumRx1016   = 0;
static char gateBlockReason[48] = "boot";
// ═══════════════════════════════════════════════════════════════
// ADVANCED EAP — MODEL YL SPLIT-BUS ROUTING
//   CAN A / MCP2515 / Party CAN : RX DAS_visualDebug.behaviorType (0x24A, DLC 8)
//   CAN B / TWAI / VH CAN      : RX/TX SCCM_turnIndicatorStalkStatus (0x249, DLC 4)
//   CAN B / TWAI / VH CAN      : RX-only UI_driverAssistControl (0x3F8) for SPR / passive telemetry
//
// Model YL also carries numeric ID 0x24A on VH CAN with DLC 4. That is a
// different bus-local frame and MUST NOT be used as DAS_visualDebug.
// ═══════════════════════════════════════════════════════════════

#define LEFTSTALK_ID      0x249
#define VISUAL_DEBUG_ID   0x24A
#define DRIVER_ASSIST_ID  0x3F8

#define STALK_IDLE    0
#define STALK_UP_1    2
#define STALK_DOWN_1  6  // Model YL stock capture: left soft stalk

#define BLINKA_TX_PERIOD_MS         20
#define BLINKA_PULSE_MS             350
#define BLINKA_AUTO_DELAY_DEFAULT_MS 2000

static portMUX_TYPE blinkAMux = portMUX_INITIALIZER_UNLOCKED;

// Real SCCM diagnostics and rolling-counter alignment.
static volatile uint32_t rx249 = 0;
static volatile uint8_t realCounter = 0;
static volatile uint8_t realTurn = 0;
static volatile uint8_t realCksum = 0;
static volatile bool cksumSelfTest = true;
static volatile bool seen249 = false;
static volatile uint8_t blinkACounter = 0;
static volatile uint8_t realDlc = 0;
static uint8_t realRaw249[8] = {0};


// Auto blinker state.
static volatile bool blinkAEnabled = true;
static volatile uint8_t activeTurn = STALK_IDLE;
static volatile uint8_t lastReqDir = 0;
// rev.06: restore the v1.0b-style separation between delayed trigger timing
// and the active SCCM one-shot pulse. 0x24A state changes may cancel a
// pending trigger, but must not truncate an already-started 350 ms pulse.
static volatile uint8_t oneShotTurn = STALK_IDLE;
static volatile uint32_t oneShotUntil = 0;
static volatile uint8_t autoPendingDir = 0;
static volatile uint32_t blinkADelayMs = BLINKA_AUTO_DELAY_DEFAULT_MS;
static volatile uint32_t autoFireAt = 0;
static volatile bool autoArmed = false;
static volatile uint32_t blkATxOk = 0;
static volatile uint32_t blkATxFail = 0;

// CAN B telemetry used by the auto blinker.
static volatile uint8_t visualBehaviorType = 0;
static volatile uint32_t visualDebugRxCount = 0;
static volatile uint32_t visualDebugLastMs = 0;

// 0x3F8 is passive RX only in rev.07.
// These observed stock values are retained for diagnostics; T-2CAN no longer
// modifies or retransmits UI_ulcSpeedConfig / UI_ulcBlindSpotConfig.
static volatile uint8_t uiUlcBlindSpotConfig = 0;
static volatile uint8_t uiUlcSpeedConfig = 0;

// CAN B load-shedding / queue telemetry.
// rev.05: priority policy is state-scoped so normal/AP driving cannot trigger
// Summon queue flushing or aggressive Summon load shedding.
static constexpr uint16_t TWAI_TX_QUEUE_LEN = 16;
static constexpr uint16_t TWAI_STANDBY_NON_SUMMON_QUEUE_LIMIT = 12;
static constexpr uint16_t TWAI_FULL_NON_SUMMON_QUEUE_LIMIT = 6;
static constexpr uint8_t  TWAI_RX_DRAIN_BUDGET = 64;

static volatile uint32_t twaiTxQueueNow = 0;
static volatile uint32_t twaiTxQueueMax = 0;
static volatile uint32_t twaiRxQueueNow = 0;
static volatile uint32_t twaiRxQueueMax = 0;
static volatile uint32_t twaiNonSummonShed = 0;
static volatile uint32_t twaiStandbyShed = 0;
static volatile uint32_t twaiFullShed = 0;
static volatile uint32_t twaiSummonQueueFlush = 0;
static volatile uint32_t twaiSummonRetryOk = 0;
static volatile uint32_t twaiSummonRetryFail = 0;
static volatile uint32_t twaiSummonTxNormal = 0;
static volatile uint32_t twaiSummonTxStandby = 0;
static volatile uint32_t twaiSummonTxFull = 0;

static const char* summonPriorityStateName(uint8_t state) {
  switch (state) {
    case SUMMON_PRIORITY_PARK_STANDBY: return "PARK_STANDBY";
    case SUMMON_PRIORITY_FULL:         return "SUMMON_FULL";
    default:                           return "NORMAL";
  }
}

// Keep the browser independent from ESP-IDF enum ordering.
static const char* twaiStateName(int state) {
  switch (state) {
    case TWAI_STATE_STOPPED:    return "STOPPED";
    case TWAI_STATE_RUNNING:    return "RUNNING";
    case TWAI_STATE_BUS_OFF:    return "BUS OFF";
    case TWAI_STATE_RECOVERING: return "RECOVERING";
    default:                    return "UNKNOWN";
  }
}

// stateMux must already be held when calling this helper.
// The most recently received *fresh* valid gear source wins. This prevents
// Original V2.3's legacy 280-stale => gateParked=true fallback from enabling
// the new priority layer while the vehicle is actually driving.
static bool summonPriorityFreshParkedLocked(uint32_t now) {
  const bool fresh280 = priorityGear280State >= 0 && priorityGear280Ms != 0 &&
                        (uint32_t)(now - priorityGear280Ms) <= SUMMON_PRIORITY_PARK_FRESH_MS;
  const bool fresh390 = priorityGear390State >= 0 && priorityGear390Ms != 0 &&
                        (uint32_t)(now - priorityGear390Ms) <= SUMMON_PRIORITY_PARK_FRESH_MS;
  if (!fresh280 && !fresh390) return false;

  if (fresh280 && (!fresh390 || (int32_t)(priorityGear280Ms - priorityGear390Ms) >= 0))
    return priorityGear280State == 1;
  return priorityGear390State == 1;
}

// stateMux must already be held when calling this helper.
// FULL can only be entered from a fresh confirmed Park context. Once FULL has
// started, it is allowed to remain FULL while the vehicle physically moves
// under Summon. A short exit grace prevents a single ACA/SPR state dropout from
// tearing down Summon priority in the middle of an otherwise active session.
static void recomputeSummonPriorityStateLocked(uint32_t now) {
  const bool freshParked = summonPriorityFreshParkedLocked(now);
  const uint8_t oldState = summonPriorityState;
  uint8_t nextState = oldState;

  if (oldState == SUMMON_PRIORITY_FULL) {
    if (gateSummoning) {
      summonPriorityFullInactiveSinceMs = 0;
    } else {
      if (summonPriorityFullInactiveSinceMs == 0)
        summonPriorityFullInactiveSinceMs = now;
      if ((uint32_t)(now - summonPriorityFullInactiveSinceMs) >= SUMMON_PRIORITY_FULL_EXIT_GRACE_MS)
        nextState = freshParked ? SUMMON_PRIORITY_PARK_STANDBY : SUMMON_PRIORITY_NORMAL;
    }
  } else {
    summonPriorityFullInactiveSinceMs = 0;
    if (gateSummoning && (oldState == SUMMON_PRIORITY_PARK_STANDBY || freshParked))
      nextState = SUMMON_PRIORITY_FULL;
    else
      nextState = freshParked ? SUMMON_PRIORITY_PARK_STANDBY : SUMMON_PRIORITY_NORMAL;
  }

  if (nextState != oldState) {
    summonPriorityState = nextState;
    summonPriorityStateSinceMs = now;
    summonPriorityTransitions++;
    if (nextState == SUMMON_PRIORITY_FULL) {
      summonPriorityFullEnterCount++;
      summonPriorityFullInactiveSinceMs = 0;
    }
    if (oldState == SUMMON_PRIORITY_FULL) {
      summonPriorityFullExitCount++;
      summonPriorityFullInactiveSinceMs = 0;
    }
  }
}

static void refreshSummonPriorityState() {
  const uint32_t now = (uint32_t)millis();
  portENTER_CRITICAL(&stateMux);
  recomputeSummonPriorityStateLocked(now);
  portEXIT_CRITICAL(&stateMux);
}

static uint8_t getSummonPriorityState() {
  uint8_t state;
  portENTER_CRITICAL(&stateMux);
  state = summonPriorityState;
  portEXIT_CRITICAL(&stateMux);
  return state;
}

static bool twaiReadQueueStatus(twai_status_info_t *out = nullptr) {
  twai_status_info_t st = {};
  if (twai_get_status_info(&st) != ESP_OK) return false;
  twaiTxQueueNow = st.msgs_to_tx;
  twaiRxQueueNow = st.msgs_to_rx;
  if (st.msgs_to_tx > twaiTxQueueMax) twaiTxQueueMax = st.msgs_to_tx;
  if (st.msgs_to_rx > twaiRxQueueMax) twaiRxQueueMax = st.msgs_to_rx;
  if (out) *out = st;
  return true;
}

// Lower-priority features never block CAN B RX. Queue reservation is scoped:
// - NORMAL: no Summon-specific shedding.
// - PARK_STANDBY: mild reservation (4 slots) for a clean Summon start.
// - SUMMON_FULL: aggressive reservation for continuous Summon injection.
static bool twaiNonSummonAdmissionOpen() {
  const uint8_t priorityState = getSummonPriorityState();

  // During normal driving there is no Summon-specific admission policy at all.
  // The following twai_transmit(..., 0) remains non-blocking and is allowed to
  // succeed/fail directly without an extra status query on every injected frame.
  if (priorityState == SUMMON_PRIORITY_NORMAL) return true;

  twai_status_info_t st = {};
  if (!twaiReadQueueStatus(&st)) return false;

  const uint16_t limit = (priorityState == SUMMON_PRIORITY_FULL)
                       ? TWAI_FULL_NON_SUMMON_QUEUE_LIMIT
                       : TWAI_STANDBY_NON_SUMMON_QUEUE_LIMIT;

  if (st.msgs_to_tx >= limit) {
    twaiNonSummonShed++;
    if (priorityState == SUMMON_PRIORITY_PARK_STANDBY) twaiStandbyShed++;
    if (priorityState == SUMMON_PRIORITY_FULL) twaiFullShed++;
    return false;
  }
  return true;
}

// Summon TX transport is state-scoped and non-blocking on the CAN B RX task.
// NORMAL: no destructive priority behavior.
// PARK_STANDBY: queue headroom is reserved by non-Summon admission control.
// SUMMON_FULL: stale pending T-2CAN TX may be flushed to protect the newest
//              Summon mux1 injection. Queue clear is NEVER used outside FULL.
static esp_err_t twaiTransmitSummonPriority(const twai_message_t *msg) {
  const uint8_t priorityState = getSummonPriorityState();

  if (priorityState == SUMMON_PRIORITY_NORMAL) {
    const esp_err_t err = twai_transmit(msg, 0);
    if (err == ESP_OK) twaiSummonTxNormal++;
    return err;
  }

  if (priorityState == SUMMON_PRIORITY_PARK_STANDBY) {
    const esp_err_t err = twai_transmit(msg, 0);
    if (err == ESP_OK) twaiSummonTxStandby++;
    return err;
  }

  // SUMMON_FULL only: keep stale pending injections from delaying the newest
  // unlock frame. The currently transmitting hardware frame is not cleared.
  twai_status_info_t st = {};
  if (twaiReadQueueStatus(&st) && st.msgs_to_tx >= (TWAI_TX_QUEUE_LEN - 2)) {
    if (twai_clear_transmit_queue() == ESP_OK) twaiSummonQueueFlush++;
  }

  esp_err_t err = twai_transmit(msg, 0);
  if (err == ESP_OK) {
    twaiSummonTxFull++;
    return ESP_OK;
  }

  // Only queue saturation gets a destructive retry. Driver/bus state errors
  // are left to the existing recovery supervisor.
  if (err != ESP_ERR_TIMEOUT) {
    twaiSummonRetryFail++;
    return err;
  }

  if (twai_clear_transmit_queue() == ESP_OK) twaiSummonQueueFlush++;
  err = twai_transmit(msg, 0);
  if (err == ESP_OK) {
    twaiSummonRetryOk++;
    twaiSummonTxFull++;
  } else {
    twaiSummonRetryFail++;
  }
  return err;
}

static inline uint32_t readBitsLE(const uint8_t *data, int startBit, int len) {
  uint32_t val = 0;
  for (int i = 0; i < len; i++) {
    int totalBit = startBit + i;
    int byteIdx = totalBit / 8;
    int bitIdx = totalBit % 8;
    if ((data[byteIdx] >> bitIdx) & 0x01) val |= (1UL << i);
  }
  return val;
}


// rev.09 Model YL SCCM_leftStalk (0x249) checksum model.
//
// Validated against the user's stock Model YL passive capture:
//   - 256 / 256 captured frames matched.
//   - DLC = 4
//   - byte 1 low nibble = rolling counter
//   - byte 2 low nibble = turn value
//   - RIGHT soft = 2, LEFT soft = 6
//
// CRC input excludes the counter nibble itself and preserves the rest of the
// live stock payload:
//   { byte1 & 0xF0, byte2, byte3, 0x00 }
// CRC-8 polynomial: 0x2F, initial value 0x00, MSB-first.
// The result is XORed with the counter-specific data-ID byte below.
static const uint8_t CKSUM_CTR[16] = {
  0x9B, 0xE8, 0x2A, 0xD3, 0xD3, 0x83, 0x4C, 0x5E,
  0x3F, 0x5E, 0xE2, 0x28, 0x3A, 0x13, 0xAF, 0xCE
};

static inline uint8_t sccm249Crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0x00;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x2F)
                         : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static inline uint8_t leftStalkChecksum(const uint8_t frame[4], uint8_t counter) {
  const uint8_t crcInput[4] = {
    (uint8_t)(frame[1] & 0xF0),
    frame[2],
    frame[3],
    0x00
  };
  return (uint8_t)(sccm249Crc8(crcInput, 4) ^ CKSUM_CTR[counter & 0x0F]);
}

static inline uint8_t dirToTurn(uint8_t dir) {
  if (dir == 1) return STALK_DOWN_1;
  if (dir == 2) return STALK_UP_1;
  return STALK_IDLE;
}

// Read the real 0x249 frame on CAN B and align the injected counter.
static void handle249OnCanB(const uint8_t *data, uint8_t dlc) {
  const uint8_t safeDlc = dlc > 8 ? 8 : dlc;
  portENTER_CRITICAL(&blinkAMux);
  realDlc = safeDlc;
  memset(realRaw249, 0, sizeof(realRaw249));
  if (safeDlc) memcpy(realRaw249, data, safeDlc);

  portEXIT_CRITICAL(&blinkAMux);

  if (dlc < 3) return;

  const uint8_t cnt = data[1] & 0x0F;
  const uint8_t turn = data[2] & 0x0F;
  const uint8_t ck = data[0];

  // rev.09: the validated Model YL checksum requires all four stock bytes.
  // A short/non-YL frame is still counted/observed, but cannot pass self-test.
  const bool checksumComparable = dlc >= 4;
  const uint8_t predicted = checksumComparable ? leftStalkChecksum(data, cnt) : 0;

  portENTER_CRITICAL(&blinkAMux);
  rx249++;
  realCounter = cnt;
  realTurn = turn;
  realCksum = ck;
  cksumSelfTest = checksumComparable && (predicted == ck);
  seen249 = true;
  if (activeTurn == STALK_IDLE) blinkACounter = cnt;
  portEXIT_CRITICAL(&blinkAMux);
}

// Send SCCM_turnIndicatorStalkStatus on CAN B.
//
// rev.09 no longer fabricates a 0x249 payload from zeros. The newest real
// Model YL stock frame is used as the template so byte1 upper bits, byte2
// upper bits and byte3 remain exactly as the vehicle produced them.
// Only the rolling counter and requested turn nibble are changed, then the
// validated full-payload CRC is recalculated.
static void sendStalkFrameCanB(uint8_t turn) {
  uint8_t cnt;
  uint8_t stockTemplate[4] = {0};
  bool haveStockTemplate = false;

  portENTER_CRITICAL(&blinkAMux);
  cnt = (blinkACounter + 1) & 0x0F;
  blinkACounter = cnt;
  haveStockTemplate = seen249 && realDlc >= 4;
  if (haveStockTemplate) memcpy(stockTemplate, realRaw249, sizeof(stockTemplate));
  portEXIT_CRITICAL(&blinkAMux);

  // Do not inject a guessed SCCM frame before a real Model YL 0x249 template
  // has been observed on the bus.
  if (!haveStockTemplate) {
    portENTER_CRITICAL(&blinkAMux);
    blkATxFail++;
    portEXIT_CRITICAL(&blinkAMux);
    return;
  }

  twai_message_t out = {};
  out.identifier = LEFTSTALK_ID;
  out.data_length_code = 4;
  out.flags = 0;
  memcpy(out.data, stockTemplate, sizeof(stockTemplate));

  out.data[1] = (uint8_t)((out.data[1] & 0xF0) | (cnt & 0x0F));
  out.data[2] = (uint8_t)((out.data[2] & 0xF0) | (turn & 0x0F));
  out.data[0] = leftStalkChecksum(out.data, cnt);

  // Auto Blinker is lower priority than Summon. Never block CAN B RX.
  esp_err_t err = ESP_ERR_TIMEOUT;
  if (twaiNonSummonAdmissionOpen()) err = twai_transmit(&out, 0);
  portENTER_CRITICAL(&blinkAMux);
  if (err == ESP_OK) blkATxOk++;
  else blkATxFail++;
  portEXIT_CRITICAL(&blinkAMux);
}

// Arm a delayed trigger when behaviorType becomes LEFT/RIGHT.
static void evaluateAutoBlinker() {
  bool en;
  portENTER_CRITICAL(&blinkAMux);
  en = blinkAEnabled;
  portEXIT_CRITICAL(&blinkAMux);

  const uint32_t now = (uint32_t)millis();
  const bool noa = autoBlinkerNOAGateOpen(now);

  uint8_t behavior = visualBehaviorType;
  uint8_t reqDir = 0;

  // rev.12: Auto Blinker is NOA-only and fail-closed. General Autosteer,
  // Force Mode, and stale/missing 0x399 status cannot arm an SCCM pulse.
  if (en && noa) {
    if (behavior == 2) reqDir = 1;
    else if (behavior == 3) reqDir = 2;
  }

  portENTER_CRITICAL(&blinkAMux);

  if (reqDir != 0 && reqDir != lastReqDir && !autoArmed) {
    autoPendingDir = reqDir;
    autoFireAt = millis() + blinkADelayMs;
    autoArmed = true;
  }

  if (reqDir == 0) {
    autoArmed = false;
    autoPendingDir = 0;
    autoFireAt = 0;
  }

  lastReqDir = reqDir;
  portEXIT_CRITICAL(&blinkAMux);
}

// Generate the one-shot 350 ms CAN B pulse.
// rev.06 follows the Advanced EAP v1.0b state model:
//   autoFireAt   = delayed-trigger deadline only
//   oneShotUntil = active-pulse deadline only
// This prevents a later 0x24A behavior change from shortening or extending
// an SCCM pulse that has already started.
static void blinkATxTick() {
  static uint32_t lastTxMs = 0;
  uint32_t now = millis();
  uint8_t turn = STALK_IDLE;
  const bool noa = autoBlinkerNOAGateOpen(now);

  portENTER_CRITICAL(&blinkAMux);

  // rev.12 final guard: a delayed request is never allowed to fire unless
  // state=5 is backed by a fresh 0x399 frame. A pulse that already started
  // is allowed to finish its existing 350 ms window.
  if (!noa && autoArmed) {
    autoArmed = false;
    autoPendingDir = 0;
    autoFireAt = 0;
    lastReqDir = 0;
  }

  // 1) Delayed trigger reached its deadline -> start an independent pulse.
  if (noa && autoArmed && (int32_t)(now - autoFireAt) >= 0) {
    oneShotTurn = dirToTurn(autoPendingDir);
    oneShotUntil = (oneShotTurn != STALK_IDLE) ? (now + BLINKA_PULSE_MS) : 0;
    autoArmed = false;
    autoPendingDir = 0;
    autoFireAt = 0;
  }

  // 2) Once started, the pulse lifetime is independent of autoFireAt and
  //    subsequent behaviorType changes.
  if (oneShotTurn != STALK_IDLE && (int32_t)(oneShotUntil - now) > 0) {
    turn = oneShotTurn;
  } else {
    oneShotTurn = STALK_IDLE;
    oneShotUntil = 0;
  }

  activeTurn = turn;
  portEXIT_CRITICAL(&blinkAMux);

  if (turn == STALK_IDLE) return;
  if (now - lastTxMs < BLINKA_TX_PERIOD_MS) return;
  lastTxMs = now;
  sendStalkFrameCanB(turn);
}

// rev.07: 0x3F8 ULC configuration injection removed.
// handle1016() remains RX-only for SPR detection and passive stock telemetry.


static inline bool isDASActive(uint8_t status) {
  bool fm = forceMode;

  switch (status) {
    // ON : 3,4,5,6
    case 3:
    case 4:
    case 5:
    case 6:
      fm = true;
      break;

    // OFF : 0,1,8,9,14
    case 0:
    case 1:
    case 8:
    case 9:
    case 14:
      fm = false;
      break;

    default:
      fm = false;
      break;
  }


  portENTER_CRITICAL(&stateMux);
  forceMode = fm;
  portEXIT_CRITICAL(&stateMux);


  return status == 3 || status == 4 || status == 5 || status == 6;
}


// rev.08 NAG transport gate: inject NAG only while the current DAS/AP status is active.
// No freshness timeout is added; this intentionally uses the existing Original AP state.
static bool nagApInjectionGateOpen() {
    bool ap;
    portENTER_CRITICAL(&stateMux);
    ap = gateAPActive;
    portEXIT_CRITICAL(&stateMux);
    return ap;
}

static inline bool summonInjectionGateOpen() {
    return gateParked || gateSummoning;
}

static void recomputeSummoning() {
    gateSummoning = lastAca && sprSeen;
}

static void clearSummonOnPark() {
    gateSummoning = false;
    sprSeen       = false;
}

static void clearSummonOnParkIfAcaInactive(uint8_t gear) {
    if (gear == 1 && !lastAca)
        clearSummonOnPark();
}

static void handle280(const uint8_t *data) {
    sumRx280++;
    const uint32_t now = (uint32_t)millis();
    last280Millis = now;
    uint8_t gear = readDIGear(data);
    int     gs   = gearState(gear);
    portENTER_CRITICAL(&stateMux);
    if (gs == 1)  gateParked = true;
    if (gs == 0)  gateParked = false;
    if (gs >= 0) {
        priorityGear280State = (int8_t)gs;
        priorityGear280Ms = now;
    }
    bool aca = (data[6] & 0x04) != 0;
    if (lastAca && !aca)
        sprSeen = false;
    lastAca = aca;
    recomputeSummoning();
    clearSummonOnParkIfAcaInactive(gear);
    recomputeSummonPriorityStateLocked(now);
    portEXIT_CRITICAL(&stateMux);
}

static void handle390(const uint8_t *data) {
    sumRx390++;
    const uint32_t now = (uint32_t)millis();
    uint8_t gear = readVehicleGear(data);
    int     gs   = gearState(gear);
    if (gs < 0) return;
    portENTER_CRITICAL(&stateMux);
    priorityGear390State = (int8_t)gs;
    priorityGear390Ms = now;
    uint32_t age = now - last280Millis;
    if (last280Millis == 0 || age > PARKED_TIMEOUT_MS) {
        gateParked = (gs == 1);
        clearSummonOnParkIfAcaInactive(gear);
    }
    recomputeSummonPriorityStateLocked(now);
    portEXIT_CRITICAL(&stateMux);
}

static void handle921(const uint8_t *data) {
    sumRx921++;
    const uint32_t now = (uint32_t)millis();
    const uint8_t dasState4 = readDASState4(data);
    bool ap = isDASActive(readDASStatus(data));
    bool noa = (dasState4 == 5); // ACTIVE_NAV = Navigate on Autopilot
    bool wasAp, wasNoa;
    portENTER_CRITICAL(&stateMux);
    wasAp = gateAPActive;
    wasNoa = gateNOAActive;
    gateAPActive = ap;
    gateNOAActive = noa;
    dasAutopilotState4 = dasState4;
    lastDASStatusMillis = now;
    portEXIT_CRITICAL(&stateMux);

    // Mode B should begin with a fresh burst when AP transitions OFF -> ON.
    if (ap && !wasAp) nagModeBPhaseStartMs = now;

    // rev.12: a delayed Auto Blinker request must not survive a NOA ->
    // Autosteer/OFF transition. Freshness loss is handled independently by
    // autoBlinkerNOAGateOpen(). Do not truncate a pulse already in progress.
    if (wasNoa && !noa) {
      portENTER_CRITICAL(&blinkAMux);
      autoArmed = false;
      autoPendingDir = 0;
      autoFireAt = 0;
      lastReqDir = 0;
      portEXIT_CRITICAL(&blinkAMux);
    }
}

static void handle1016(const uint8_t *data, uint8_t dlc) {
    if (dlc < 4) return;
    sumRx1016++;
    const uint32_t now = (uint32_t)millis();
    uint8_t spr = (data[3] >> 4) & 0x0F;
    if (dlc >= 7) {
        // UI_ulcSpeedConfig: bits 50-51.
        // UI_ulcBlindSpotConfig: bits 52-53.
        uiUlcSpeedConfig = (data[6] >> 2) & 0x03;
        uiUlcBlindSpotConfig = (data[6] >> 4) & 0x03;
    }
    portENTER_CRITICAL(&stateMux);
    if (spr != 0)
        sprSeen = true;
    recomputeSummoning();
    recomputeSummonPriorityStateLocked(now);
    portEXIT_CRITICAL(&stateMux);
}

static void injectSummon(const twai_message_t &src) {
     bool en, gate, fmode;
    portENTER_CRITICAL(&stateMux);
    en   = summonEnabled;
    gate = summonInjectionGateOpen();
    fmode = forceMode;
     if (!gate && !fmode) {
        if (!gateAPActive  && !gateParked && !gateSummoning)
            strncpy(gateBlockReason, "AP-,Park-,Summon-", sizeof(gateBlockReason));
    }
    portEXIT_CRITICAL(&stateMux);
     if ((!en || !gate) && !fmode)
        return;

    twai_message_t out;
    out.identifier       = src.identifier;
    out.data_length_code = src.data_length_code;
    out.flags            = 0;
    for (int i = 0; i < 8; i++) out.data[i] = src.data[i];
    sumRxMux1++;

    // If the stock frame already carries the unlocked values, it is already
    // the desired bus state and does not need a duplicate echo.
    const bool alreadyUnlocked = !getBit(out.data, 19) && getBit(out.data, 47);
    if (alreadyUnlocked) return;

    setBit(out.data, 19, false);
    setBit(out.data, 47, true);

    // State-scoped Summon transport: NORMAL has no destructive priority behavior,
    // PARK_STANDBY reserves headroom, and only SUMMON_FULL may flush stale TX.
    esp_err_t err = twaiTransmitSummonPriority(&out);
    if (err == ESP_OK) sumTxOk++;
    else               sumTxFail++;
}

// ── TLSSC : 0x3FD mux0 bit38/39 ──
// rev.16: TLSSC has its own AP-active gate. It no longer shares or bypasses
// the Parked/Summoning gate used by Summon/EU Unlock.
static void injectTLSSC(const twai_message_t &src) {
    bool en, ap;
    portENTER_CRITICAL(&stateMux);
    en = tlsscEnabled;
    ap = gateAPActive;
    portEXIT_CRITICAL(&stateMux);

    if (!en || !ap)
        return;

    twai_message_t out;
    out.identifier       = src.identifier;
    out.data_length_code = src.data_length_code;
    out.flags            = 0;
    for (int i = 0; i < 8; i++) out.data[i] = src.data[i];
    // Avoid a duplicate if the incoming mux0 already contains both values.
    if (getBit(out.data, 38) && getBit(out.data, 39)) return;

    setBit(out.data, 38, true);   // UI_fsdStopsControlEnabled = 1
    setBit(out.data, 39, true);   // UI_fsdContinueOnGreenWithCIPV = 1

    // TLSSC is lower priority than Summon and must never block CAN B RX.
    if (!twaiNonSummonAdmissionOpen()) {
      sumTxFail++;
      return;
    }
    esp_err_t err = twai_transmit(&out, 0);
    if (err == ESP_OK) sumTxOk++;
    else               sumTxFail++;
}

static void summonCfgLoad() {
    prefs.begin("summon", false);
    summonEnabled = prefs.getBool("en", true);
    tlsscEnabled  = prefs.getBool("tlssc", false);
    blinkAEnabled = prefs.getBool("blkA", true);

    // rev.17 one-time migration: make the new 2.0 s Auto Blinker delay take
    // effect even on vehicles that already have rev.16's 3000 ms value in NVS.
    // After this migration, dashboard changes remain authoritative and persist.
    const bool delayMigratedRev17 = prefs.getBool("blkDly17", false);
    if (!delayMigratedRev17) {
      blinkADelayMs = BLINKA_AUTO_DELAY_DEFAULT_MS;
      prefs.putUInt("blkADly", blinkADelayMs);
      prefs.putBool("blkDly17", true);
    } else {
      blinkADelayMs = prefs.getUInt("blkADly", BLINKA_AUTO_DELAY_DEFAULT_MS);
    }
    blinkADelayMs = constrain((uint32_t)blinkADelayMs, (uint32_t)0, (uint32_t)30000);

    // Compatibility cleanup: remove retired configuration keys from older revisions.
    prefs.remove("tlrst");
    prefs.remove("ulcbs");
    prefs.remove("ulcsp");
    prefs.end();
}

static void summonCfgSave() {
    prefs.begin("summon", false);
    prefs.putBool("en", summonEnabled);
    prefs.putBool("tlssc", tlsscEnabled);
    prefs.putBool("blkA", blinkAEnabled);
    prefs.putUInt("blkADly", blinkADelayMs);
    prefs.end();
}

// ═══════════════════════════════════════════════════════════════
// OTA UPDATE
// ═══════════════════════════════════════════════════════════════

static volatile bool     otaInProgress = false;
static volatile bool     otaSuccess    = false;
static volatile bool     otaError      = false;
static volatile uint32_t otaBytes      = 0;
static volatile uint32_t otaTotal      = 0;
static char              otaErrMsg[64] = "";

// ═══════════════════════════════════════════════════════════════
// WEB SERVER
// ═══════════════════════════════════════════════════════════════

extern const char INDEX_HTML[] PROGMEM;
static WebServer server(80);

static String nagCfgToJson() {
  NagConfig c;
  portENTER_CRITICAL(&nagCfgMux);
  c = nagCfg;
  portEXIT_CRITICAL(&nagCfgMux);
  String s;
  s.reserve(512);
  s = "{";
  s += "\"enabled\":";    s += (c.enabled ? "true" : "false");
  s += ",\"mode\":";      s += String(c.mode);
  s += ",\"targetId\":";  s += String(c.targetId);
  s += ",\"hoRatePct\":"; s += String(c.hoRatePct);
  s += ",\"burstMs\":";   s += String(c.burstMs);
  s += ",\"pauseMs\":";   s += String(c.pauseMs);
  s += ",\"apStateId\":"; s += String(c.apStateId);
  s += ",\"steeringId\":";s += String(c.steeringId);
s += ",\"torque\":[";
for (uint8_t i = 0; i < c.torqueCount; i++) {
  if (i) s += ",";
  s += "{\"b2\":";
  s += String(c.torqueB2[i]);
  s += ",\"b3\":";
  s += String(c.torqueB3[i]);
  uint16_t raw = ((c.torqueB2[i] & 0x0F) << 8) | c.torqueB3[i];
  float nm = raw * 0.01f - 20.5f;
  s += ",\"nm\":";
  s += String(nm, 2);
  s += "}";
}
s += "]}";
return s;
}

static String nagStatsToJson() {
  NagContext c;
  portENTER_CRITICAL(&nagCtxMux); c = nagCtx; portEXIT_CRITICAL(&nagCtxMux);
  String s;
  s.reserve(512);
  s = "{";
  s += "\"rx\":";            s += String(nagRxFrames);
  s += ",\"echo\":";         s += String(nagEchoCount);
  s += ",\"txOk\":";         s += String(mcpTxOk);
  s += ",\"txFail\":";       s += String(mcpTxFail);
  s += ",\"latUs\":";        s += String(nagEchoLatUs);
  s += ",\"ho\":";           s += String(nagRealHo);
  s += ",\"torque\":";       s += String(nagRealTorque, 2);
  s += ",\"injHo\":";        s += String(nagLastInjectedHo);
  s += ",\"injNm\":";        s += String(nagLastInjectedNm, 2);
  s += ",\"uptimeS\":";      s += String((millis() - bootTime) / 1000);
  s += ",\"apState\":";      s += String(c.apState);
  s += ",\"handsOnState\":"; s += String(c.handsOnState);
  s += ",\"steeringDeg\":";  s += String(c.steeringAngleDeg, 1);
  unsigned long now = millis();
  s += ",\"apStaleMs\":";    s += String((c.lastApStateMs == 0) ? 999999 : (now - c.lastApStateMs));
  s += ",\"stStaleMs\":";    s += String((c.lastSteeringMs == 0) ? 999999 : (now - c.lastSteeringMs));
  s += ",\"canAState\":";    s += String((int)mcpState);
  s += ",\"apActive\":";     s += (nagApInjectionGateOpen() ? "true" : "false");
  s += "}";
  return s;
}

static String summonStatsToJson() {
    bool en, tlssc, ap, parked, summon, aca, spr, fmode, priorityFreshParked;
    uint8_t priorityState;
    uint32_t prioritySince, priorityTransitions, priorityFullEnter, priorityFullExit, priorityFullInactiveSince;
    uint32_t rmx, tok, tfail, r280, r390, r921, r1016;
    portENTER_CRITICAL(&stateMux);
    en     = summonEnabled;
    tlssc  = tlsscEnabled;
    ap     = gateAPActive;
    parked = gateParked;
    summon = gateSummoning;
    aca    = lastAca;
    spr    = sprSeen;
    fmode  = forceMode;
    priorityState = summonPriorityState;
    priorityFreshParked = summonPriorityFreshParkedLocked((uint32_t)millis());
    prioritySince = summonPriorityStateSinceMs;
    priorityTransitions = summonPriorityTransitions;
    priorityFullEnter = summonPriorityFullEnterCount;
    priorityFullExit = summonPriorityFullExitCount;
    priorityFullInactiveSince = summonPriorityFullInactiveSinceMs;
    rmx    = sumRxMux1;
    tok    = sumTxOk;
    tfail  = sumTxFail;
    r280   = sumRx280;
    r390   = sumRx390;
    r921   = sumRx921;
    r1016  = sumRx1016;
    portEXIT_CRITICAL(&stateMux);
    bool gate = parked || summon;
    twai_status_info_t st = {};
    const bool twaiStatusOk = (twai_get_status_info(&st) == ESP_OK);
    String s = "{";
    s += "\"enabled\":"  + String(en     ? "true" : "false");
    s += ",\"tlssc\":"   + String(tlssc  ? "true" : "false");
    s += ",\"gate\":"    + String(gate   ? "true" : "false");
    s += ",\"ap\":"      + String(ap     ? "true" : "false");
    s += ",\"parked\":"  + String(parked ? "true" : "false");
    s += ",\"summon\":"  + String(summon ? "true" : "false");
    s += ",\"aca\":"     + String(aca    ? "true" : "false");
    s += ",\"spr\":"     + String(spr    ? "true" : "false");
    s += ",\"forceMode\":"+ String(fmode ? "true" : "false");
    s += ",\"priorityState\":" + String((int)priorityState);
    s += ",\"priorityStateName\":\"" + String(summonPriorityStateName(priorityState)) + "\"";
    s += ",\"priorityFreshParked\":" + String(priorityFreshParked ? "true" : "false");
    s += ",\"priorityStateSinceMs\":" + String((unsigned long)prioritySince);
    s += ",\"priorityTransitions\":" + String((unsigned long)priorityTransitions);
    s += ",\"priorityFullEnter\":" + String((unsigned long)priorityFullEnter);
    s += ",\"priorityFullExit\":" + String((unsigned long)priorityFullExit);
    s += ",\"priorityExitGraceActive\":" + String(priorityFullInactiveSince != 0 ? "true" : "false");
    s += ",\"txQueueNow\":" + String((unsigned long)twaiTxQueueNow);
    s += ",\"txQueueMax\":" + String((unsigned long)twaiTxQueueMax);
    s += ",\"nonSummonShed\":" + String((unsigned long)twaiNonSummonShed);
    s += ",\"standbyShed\":" + String((unsigned long)twaiStandbyShed);
    s += ",\"fullShed\":" + String((unsigned long)twaiFullShed);
    s += ",\"summonQueueFlush\":" + String((unsigned long)twaiSummonQueueFlush);
    s += ",\"summonTxNormal\":" + String((unsigned long)twaiSummonTxNormal);
    s += ",\"summonTxStandby\":" + String((unsigned long)twaiSummonTxStandby);
    s += ",\"summonTxFull\":" + String((unsigned long)twaiSummonTxFull);
    s += ",\"rxMux1\":"  + String(rmx);
    s += ",\"txOk\":"    + String(tok);
    s += ",\"txFail\":"  + String(tfail);
    s += ",\"rx280\":"   + String(r280);
    s += ",\"rx390\":"   + String(r390);
    s += ",\"rx921\":"   + String(r921);
    s += ",\"rx1016\":"  + String(r1016);
    s += ",\"canState\":" + String(twaiStatusOk ? (int)st.state : -1);
    s += ",\"canStateName\":\"" + String(twaiStatusOk ? twaiStateName(st.state) : "UNAVAILABLE") + "\"";
    s += ",\"uptimeS\":"  + String((millis() - bootTime) / 1000);
    s += "}";
    return s;
}


static String blinkAStatsToJson() {
  bool en, ap, noaRaw, noaEffective, noaFresh, fmode;
  uint8_t dasState4;
  uint8_t curTurn, pending;
  uint32_t delayMs, remain, txOk, txFail, r249;
  bool armed, seen, selfTest;
  uint8_t rCnt, rTurn, rCk, rDlc;
  uint8_t raw249[8] = {0};
  portENTER_CRITICAL(&blinkAMux);
  en = blinkAEnabled;
  curTurn = activeTurn;
  pending = autoPendingDir;
  delayMs = blinkADelayMs;
  armed = autoArmed;
  uint32_t now = millis();
  remain = (autoArmed && (int32_t)(autoFireAt - now) > 0) ? (autoFireAt - now) : 0;
  txOk = blkATxOk;
  txFail = blkATxFail;
  r249 = rx249;
  rCnt = realCounter;
  rTurn = realTurn;
  rCk = realCksum;
  seen = seen249;
  selfTest = cksumSelfTest;
  rDlc = realDlc;
  memcpy(raw249, realRaw249, sizeof(raw249));
  portEXIT_CRITICAL(&blinkAMux);
  uint32_t noaLastMs;
  portENTER_CRITICAL(&stateMux);
  ap = gateAPActive;
  noaRaw = gateNOAActive;
  dasState4 = dasAutopilotState4;
  noaLastMs = lastDASStatusMillis;
  fmode = forceMode;
  portEXIT_CRITICAL(&stateMux);

  const uint32_t noaNow = (uint32_t)millis();
  const uint32_t noaAgeMs = (noaLastMs == 0) ? UINT32_MAX : (uint32_t)(noaNow - noaLastMs);
  noaFresh = (noaLastMs != 0 && noaAgeMs <= NOA_STATUS_FRESH_MS);
  noaEffective = noaRaw && noaFresh;

  String s = "{";
  s += "\"enabled\":" + String(en ? "true" : "false");
  s += ",\"apActive\":" + String(ap ? "true" : "false");
  s += ",\"noaActive\":" + String(noaEffective ? "true" : "false");
  s += ",\"noaRawActive\":" + String(noaRaw ? "true" : "false");
  s += ",\"noaFresh\":" + String(noaFresh ? "true" : "false");
  s += ",\"noaAgeMs\":" + String((noaAgeMs == UINT32_MAX) ? 999999UL : (unsigned long)noaAgeMs);
  s += ",\"noaFreshLimitMs\":" + String((unsigned long)NOA_STATUS_FRESH_MS);
  s += ",\"dasState\":" + String((int)dasState4);
  s += ",\"forceMode\":" + String(fmode ? "true" : "false");
  s += ",\"behaviorType\":" + String((int)visualBehaviorType);
  s += ",\"activeTurn\":" + String(curTurn);
  s += ",\"delayMs\":" + String(delayMs);
  s += ",\"autoArmed\":" + String(armed ? "true" : "false");
  s += ",\"autoPending\":" + String(pending);
  s += ",\"autoRemainMs\":" + String(remain);
  s += ",\"txOk\":" + String(txOk);
  s += ",\"txFail\":" + String(txFail);
  s += ",\"rx249\":" + String(r249);
  s += ",\"seen249\":" + String(seen ? "true" : "false");
  s += ",\"realCounter\":" + String(rCnt);
  s += ",\"realTurn\":" + String(rTurn);
  s += ",\"realCksum\":" + String(rCk);
  s += ",\"realDlc\":" + String(rDlc);
  String rawHex;
  rawHex.reserve(24);
  for (uint8_t i = 0; i < rDlc; i++) {
    if (i) rawHex += " ";
    if (raw249[i] < 0x10) rawHex += "0";
    rawHex += String(raw249[i], HEX);
  }
  rawHex.toUpperCase();
  s += ",\"realRaw\":\"" + rawHex + "\"";
  s += ",\"cksumSelfTest\":" + String(selfTest ? "true" : "false");
  s += ",\"canBState\":" + String((int)twaiReady);
  s += ",\"uptimeS\":" + String((millis() - bootTime) / 1000);
  s += "}";
  return s;
}

static String dasTelemetryStatsToJson() {
  uint32_t now = millis();
  String s = "{";
  s += "\"behaviorType\":" + String((int)visualBehaviorType);
  s += ",\"ulcBlindSpotConfig\":" + String((int)uiUlcBlindSpotConfig);
  s += ",\"ulcSpeedConfig\":" + String((int)uiUlcSpeedConfig);
  s += ",\"visualDebugRx\":" + String((unsigned long)visualDebugRxCount);
  s += ",\"visualDebugStaleMs\":" + String(visualDebugLastMs == 0 ? 999999UL : (now - visualDebugLastMs));
  s += "}";
  return s;
}

static String systemStatsToJson() {
  String s = "{";
  s += "\"fwVersion\":\"" + String(FW_VERSION) + "\"";
  s += ",\"freeHeap\":"      + String(ESP.getFreeHeap());
  s += ",\"uptimeS\":"      + String((millis() - bootTime) / 1000);
  s += ",\"mcpReady\":"     + String(mcpReady  ? "true" : "false");
  s += ",\"twaiReady\":"    + String(twaiReady ? "true" : "false");
  s += ",\"rtcBootCount\":" + String((unsigned long)rtcBootCount);
  s += ",\"runtimeStatsResetCount\":" + String((unsigned long)runtimeStatsResetCount);
  s += ",\"runtimeStatsLastResetMs\":" + String((unsigned long)runtimeStatsLastResetMs);
  s += ",\"canHardReinit\":" + String((unsigned long)canHardReinitCount);
  s += ",\"canHardReinitFail\":" + String((unsigned long)canHardReinitFailCount);
  s += ",\"canLastHardReason\":" + String((int)canLastHardReinitReason);
  s += ",\"canRecoverySleeping\":" + String(recoverySleeping ? "true" : "false");
  s += ",\"twaiTxQueueNow\":" + String((unsigned long)twaiTxQueueNow);
  s += ",\"twaiTxQueueMax\":" + String((unsigned long)twaiTxQueueMax);
  s += ",\"twaiRxQueueNow\":" + String((unsigned long)twaiRxQueueNow);
  s += ",\"twaiRxQueueMax\":" + String((unsigned long)twaiRxQueueMax);
  s += ",\"twaiNonSummonShed\":" + String((unsigned long)twaiNonSummonShed);
  s += ",\"twaiStandbyShed\":" + String((unsigned long)twaiStandbyShed);
  s += ",\"twaiFullShed\":" + String((unsigned long)twaiFullShed);
  s += ",\"summonPriorityState\":" + String((int)getSummonPriorityState());
  s += ",\"summonPriorityStateName\":\"" + String(summonPriorityStateName(getSummonPriorityState())) + "\"";
  s += ",\"twaiSummonTxNormal\":" + String((unsigned long)twaiSummonTxNormal);
  s += ",\"twaiSummonTxStandby\":" + String((unsigned long)twaiSummonTxStandby);
  s += ",\"twaiSummonTxFull\":" + String((unsigned long)twaiSummonTxFull);
  s += ",\"twaiSummonQueueFlush\":" + String((unsigned long)twaiSummonQueueFlush);
  s += ",\"twaiSummonRetryOk\":" + String((unsigned long)twaiSummonRetryOk);
  s += ",\"twaiSummonRetryFail\":" + String((unsigned long)twaiSummonRetryFail);
  s += ",\"otaInProgress\":" + String(otaInProgress ? "true" : "false");
  s += ",\"otaSuccess\":"    + String(otaSuccess    ? "true" : "false");
  s += ",\"otaError\":"      + String(otaError      ? "true" : "false");
  s += ",\"otaErrMsg\":\""   + String(otaErrMsg) + "\"";
  s += ",\"otaBytes\":"      + String(otaBytes);
  s += ",\"otaTotal\":"      + String(otaTotal);
  s += "}";
  return s;
}

// ─── Boot timing capture export ─────────────────────────────

static const char* bootCaptureHardReasonName(uint8_t reason) {
  switch (reason) {
    case CAN_SUP_HARD_ACQUIRE: return "ACQUIRE";
    case CAN_SUP_HARD_STALE:   return "STALE";
    case CAN_SUP_HARD_MANUAL:  return "MANUAL";
    default:                   return "UNKNOWN";
  }
}

static void bootCaptureAppendEvent(String &out, const char *event, uint32_t t, const String &detail = String()) {
  out += event;
  out += ",";
  if (t == BOOT_CAPTURE_UNSET) out += "-1";
  else out += String((unsigned long)t);
  out += ",\"";
  out += detail;
  out += "\"\n";
}

static String bootCaptureToCsv() {
  uint32_t canInitDone, canTasks, wifiReady, firstA, firstB;
  uint32_t first370, first370Torque, first399, first24A, first249;
  uint16_t first370Raw, first370TorqueRaw;
  uint8_t hardCount;
  uint32_t hardDropped;
  BootHardReinitEvent hard[BOOT_CAPTURE_HARD_MAX];

  portENTER_CRITICAL(&bootCaptureMux);
  canInitDone = bootCapCanInitDoneMs;
  canTasks = bootCapCanTasksStartedMs;
  wifiReady = bootCapWifiReadyMs;
  firstA = bootCapFirstCanAMs;
  firstB = bootCapFirstCanBMs;
  first370 = bootCapFirst370Ms;
  first370Torque = bootCapFirst370TorqueMs;
  first399 = bootCapFirst399Ms;
  first24A = bootCapFirstParty24AMs;
  first249 = bootCapFirstVh249Ms;
  first370Raw = bootCapFirst370Raw;
  first370TorqueRaw = bootCapFirst370TorqueRaw;
  hardCount = bootCapHardCount;
  hardDropped = bootCapHardDropped;
  for (uint8_t i = 0; i < hardCount && i < BOOT_CAPTURE_HARD_MAX; i++) hard[i] = bootCapHard[i];
  portEXIT_CRITICAL(&bootCaptureMux);

  String out;
  out.reserve(2200);
  out = "event,time_ms,detail\n";
  bootCaptureAppendEvent(out, "BOOT_SETUP_START", 0, String(FW_VERSION));
  bootCaptureAppendEvent(out, "CAN_INIT_DONE", canInitDone);
  bootCaptureAppendEvent(out, "CAN_RX_TASKS_STARTED", canTasks);
  bootCaptureAppendEvent(out, "WIFI_AP_READY", wifiReady);
  bootCaptureAppendEvent(out, "FIRST_CAN_A_ANY", firstA, "Party/MCP2515");
  bootCaptureAppendEvent(out, "FIRST_CAN_B_ANY", firstB, "VH/TWAI");

  String d370;
  if (first370Raw != 0xFFFF) {
    const float nm = first370Raw * 0.01f - 20.5f;
    d370 = "raw=" + String((unsigned)first370Raw) + ";torque_nm=" + String(nm, 2);
  } else d370 = "not_seen";
  bootCaptureAppendEvent(out, "FIRST_PARTY_0x370", first370, d370);

  String dTorque;
  if (first370TorqueRaw != 0xFFFF) {
    const float nm = first370TorqueRaw * 0.01f - 20.5f;
    dTorque = "abs_torque_ge_0.10Nm;raw=" + String((unsigned)first370TorqueRaw) + ";torque_nm=" + String(nm, 2);
  } else dTorque = "not_seen";
  bootCaptureAppendEvent(out, "FIRST_0x370_ABS_TORQUE_GE_0.10NM", first370Torque, dTorque);

  bootCaptureAppendEvent(out, "FIRST_PARTY_0x399", first399, "DAS/AP state");
  bootCaptureAppendEvent(out, "FIRST_PARTY_0x24A_DLC8", first24A, "DAS visual debug / Auto Blinker source");
  bootCaptureAppendEvent(out, "FIRST_VH_0x249_DLC4", first249, "SCCM stalk status");

  for (uint8_t i = 0; i < hardCount && i < BOOT_CAPTURE_HARD_MAX; i++) {
    String startName = "HARD_REINIT_" + String((unsigned)(i + 1)) + "_START";
    String endName = "HARD_REINIT_" + String((unsigned)(i + 1)) + "_END";
    String detail = "reason=" + String(bootCaptureHardReasonName(hard[i].reason));
    bootCaptureAppendEvent(out, startName.c_str(), hard[i].startMs, detail);
    String endDetail = detail + ";success=" + String(hard[i].success == 1 ? "1" : hard[i].success == 0 ? "0" : "in_progress");
    bootCaptureAppendEvent(out, endName.c_str(), hard[i].endMs, endDetail);
  }

  bootCaptureAppendEvent(out, "EXPORT", bootCaptureNowMs(),
    "hard_reinit_events=" + String((unsigned)hardCount) +
    ";hard_reinit_dropped=" + String((unsigned long)hardDropped) +
    ";mcp_rx_count=" + String((unsigned long)mcpRxCount) +
    ";vh_rx_count=" + String((unsigned long)canRxBeat));
  return out;
}

static void httpBootCaptureCsv() {
  server.sendHeader("Content-Disposition", "attachment; filename=T2CAN_boot_capture.csv");
  server.send(200, "text/csv", bootCaptureToCsv());
}

// ─── OTA update ─────────────────────────────────────────────

static void httpOtaUpload() {
    HTTPUpload &up = server.upload();

    if (up.status == UPLOAD_FILE_START) {
        otaInProgress = true;
        otaSuccess    = false;
        otaError      = false;
        otaBytes      = 0;
        otaErrMsg[0]  = '\0';
        Serial.printf("[OTA] Start: %s\n", up.filename.c_str());

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            otaError = true;
            strncpy(otaErrMsg, Update.errorString(), sizeof(otaErrMsg) - 1);
            Serial.printf("[OTA] begin() failed: %s\n", otaErrMsg);
        }
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (!otaError && Update.write(up.buf, up.currentSize) != up.currentSize) {
            otaError = true;
            strncpy(otaErrMsg, Update.errorString(), sizeof(otaErrMsg) - 1);
            Serial.printf("[OTA] write() failed: %s\n", otaErrMsg);
        }
        otaBytes += up.currentSize;
    } else if (up.status == UPLOAD_FILE_END) {
        if (!otaError && Update.end(true)) {
            otaSuccess = true;
            otaTotal   = otaBytes;
            Serial.printf("[OTA] Success: %u bytes\n", up.totalSize);
        } else if (!otaError) {
            otaError = true;
            strncpy(otaErrMsg, Update.errorString(), sizeof(otaErrMsg) - 1);
            Serial.printf("[OTA] end() failed: %s\n", otaErrMsg);
        }
        otaInProgress = false;
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        otaInProgress = false;
        otaError      = true;
        strncpy(otaErrMsg, "aborted", sizeof(otaErrMsg) - 1);
        Serial.println("[OTA] Aborted");
    }
}

static void httpOtaFinish() {
    bool ok = otaSuccess && !otaError;
    String resp = String("{\"ok\":") + (ok ? "true" : "false") +
                  ",\"error\":\"" + String(otaErrMsg) + "\"}";
    server.sendHeader("Connection", "close");
    server.send(200, "application/json", resp);
    if (ok) {
        delay(700);
        ESP.restart();
    }
}

static void httpSystemStats() { server.send(200, "application/json", systemStatsToJson()); }


static void httpCanHardReinit() {
  requestCanSubsystemRestart(CAN_SUP_HARD_MANUAL);
  server.send(202, "application/json", "{\"ok\":true,\"action\":\"hard-can-reinit-requested\"}");
}

static void httpRebootT2Can() {
  server.send(200, "application/json", "{\"ok\":true,\"action\":\"rebooting\"}");
  delay(250);
  ESP.restart();
}

static void httpRoot()   { server.send_P(200, "text/html", INDEX_HTML); }
static void httpNagConfig() { server.send(200, "application/json", nagCfgToJson()); }
static void httpNagStats()  { server.send(200, "application/json", nagStatsToJson()); }

static void httpNagSetMode() {
  int m = server.arg("m").toInt();
  NagConfig nc;
  if      (m == 1) nagCfgDefaultsModeB(nc);
  else             nagCfgDefaultsModeA(nc);
  portENTER_CRITICAL(&nagCfgMux); nagCfg = nc; portEXIT_CRITICAL(&nagCfgMux);
  nagCfgSave();
  server.send(200, "application/json", nagCfgToJson());
}

static void httpNagUpdate() {
  NagConfig nc;
  portENTER_CRITICAL(&nagCfgMux); nc = nagCfg; portEXIT_CRITICAL(&nagCfgMux);
  if (server.hasArg("enabled"))
    nc.enabled = (server.arg("enabled") == "1");
  if (server.hasArg("targetId")) {
    char* endptr;
    long val = strtol(server.arg("targetId").c_str(), &endptr, 0);
    if (*endptr == '\0' && val > 0 && val <= 0x7FF)
      nc.targetId = (uint16_t)val;
  }
  if (server.hasArg("hoRatePct")) {
    int val = server.arg("hoRatePct").toInt();
    if (val >= 0 && val <= 100) nc.hoRatePct = (uint8_t)val;
  }
  if (server.hasArg("burstMs")) {
    int val = server.arg("burstMs").toInt();
    if (val >= 50 && val <= 10000) nc.burstMs = (uint16_t)val;
  }
  if (server.hasArg("pauseMs")) {
    int val = server.arg("pauseMs").toInt();
    if (val >= 0 && val <= 10000) nc.pauseMs = (uint16_t)val;
  }
  if (server.hasArg("apStateId")) {
    char* endptr;
    long val = strtol(server.arg("apStateId").c_str(), &endptr, 0);
    if (*endptr == '\0' && val > 0 && val <= 0x7FF)
      nc.apStateId = (uint16_t)val;
  }
  if (server.hasArg("steeringId")) {
    char* endptr;
    long val = strtol(server.arg("steeringId").c_str(), &endptr, 0);
    if (*endptr == '\0' && val > 0 && val <= 0x7FF)
      nc.steeringId = (uint16_t)val;
  }
  if (server.hasArg("count")) {
    uint8_t n = (uint8_t)server.arg("count").toInt();
    if (n > NAG_MAX_TORQUE_ENTRIES) n = NAG_MAX_TORQUE_ENTRIES;
    if (n < 1) n = 1;
    for (uint8_t i = 0; i < n; i++) {
      String k2 = "b2_" + String(i);
      String k3 = "b3_" + String(i);
      if (server.hasArg(k2)) {
        char* endptr;
        long val = strtol(server.arg(k2).c_str(), &endptr, 0);
        if (*endptr == '\0' && val >= 0 && val <= 255)
          nc.torqueB2[i] = (uint8_t)val;
      }
      if (server.hasArg(k3)) {
        char* endptr;
        long val = strtol(server.arg(k3).c_str(), &endptr, 0);
        if (*endptr == '\0' && val >= 0 && val <= 255)
          nc.torqueB3[i] = (uint8_t)val;
      }
    }
    nc.torqueCount = n;
  }
  nagCfgClampAll(nc);
  portENTER_CRITICAL(&nagCfgMux); nagCfg = nc; portEXIT_CRITICAL(&nagCfgMux);
  nagCfgSave();
  server.send(200, "application/json", nagCfgToJson());
}

static void httpNagReset() {
  NagConfig nc;
  nagCfgDefaultsModeA(nc);
  portENTER_CRITICAL(&nagCfgMux); nagCfg = nc; portEXIT_CRITICAL(&nagCfgMux);
  nagCfgSave();
  nagRxFrames = nagEchoCount = mcpTxOk = mcpTxFail = 0;
  server.send(200, "application/json", nagCfgToJson());
}

static void httpSummonStats()  { server.send(200, "application/json", summonStatsToJson()); }
static void httpSummonEnable() {
    portENTER_CRITICAL(&stateMux); summonEnabled = true;  portEXIT_CRITICAL(&stateMux);
    summonCfgSave();
    server.send(200, "application/json", summonStatsToJson());
}
static void httpSummonDisable() {
    portENTER_CRITICAL(&stateMux); summonEnabled = false; portEXIT_CRITICAL(&stateMux);
    summonCfgSave();
    server.send(200, "application/json", summonStatsToJson());
}
static void httpSummonTlsscEnable() {
    portENTER_CRITICAL(&stateMux); tlsscEnabled = true;  portEXIT_CRITICAL(&stateMux);
    summonCfgSave();
    server.send(200, "application/json", summonStatsToJson());
}
static void httpSummonTlsscDisable() {
    portENTER_CRITICAL(&stateMux); tlsscEnabled = false; portEXIT_CRITICAL(&stateMux);
    summonCfgSave();
    server.send(200, "application/json", summonStatsToJson());
}

static void httpSummonForceMode() {
    portENTER_CRITICAL(&stateMux);
    forceMode = !forceMode;
    portEXIT_CRITICAL(&stateMux);
    evaluateAutoBlinker();
    server.send(200, "application/json", summonStatsToJson());
}


static void httpDasTelemetryStats() {
  server.send(200, "application/json", dasTelemetryStatsToJson());
}

static void httpBlinkAStats() {
  server.send(200, "application/json", blinkAStatsToJson());
}

static void httpBlinkAEnable() {
  portENTER_CRITICAL(&blinkAMux);
  blinkAEnabled = true;
  portEXIT_CRITICAL(&blinkAMux);
  evaluateAutoBlinker();
  summonCfgSave();
  server.send(200, "application/json", blinkAStatsToJson());
}

static void httpBlinkADisable() {
  portENTER_CRITICAL(&blinkAMux);
  blinkAEnabled = false;
  autoArmed = false;
  autoPendingDir = 0;
  autoFireAt = 0;
  oneShotTurn = STALK_IDLE;
  oneShotUntil = 0;
  activeTurn = STALK_IDLE;
  lastReqDir = 0;
  portEXIT_CRITICAL(&blinkAMux);
  summonCfgSave();
  server.send(200, "application/json", blinkAStatsToJson());
}

static void httpBlinkADelay() {
  int v = server.hasArg("ms") ? server.arg("ms").toInt() : (int)BLINKA_AUTO_DELAY_DEFAULT_MS;
  v = constrain(v, 0, 30000);
  portENTER_CRITICAL(&blinkAMux);
  blinkADelayMs = (uint32_t)v;
  portEXIT_CRITICAL(&blinkAMux);
  summonCfgSave();
  server.send(200, "application/json", blinkAStatsToJson());
}

// Reset only diagnostic/session counters. No NVS/configuration, live feature state,
// CAN liveness timestamps, or mcpRxCount warmup state is modified.
static void resetRuntimeStats() {
  nagRxFrames = 0;
  nagEchoCount = 0;
  mcpTxOk = 0;
  mcpTxFail = 0;
  nagEchoLatUs = 0;

  portENTER_CRITICAL(&stateMux);
  sumRxMux1 = 0;
  sumTxOk = 0;
  sumTxFail = 0;
  sumRx280 = 0;
  sumRx390 = 0;
  sumRx921 = 0;
  sumRx1016 = 0;
  summonPriorityTransitions = 0;
  summonPriorityFullEnterCount = 0;
  summonPriorityFullExitCount = 0;
  portEXIT_CRITICAL(&stateMux);

  portENTER_CRITICAL(&blinkAMux);
  rx249 = 0;
  blkATxOk = 0;
  blkATxFail = 0;
  portEXIT_CRITICAL(&blinkAMux);
  visualDebugRxCount = 0;

  twaiReadQueueStatus();
  twaiTxQueueMax = twaiTxQueueNow;
  twaiRxQueueMax = twaiRxQueueNow;
  twaiNonSummonShed = 0;
  twaiStandbyShed = 0;
  twaiFullShed = 0;
  twaiSummonQueueFlush = 0;
  twaiSummonRetryOk = 0;
  twaiSummonRetryFail = 0;
  twaiSummonTxNormal = 0;
  twaiSummonTxStandby = 0;
  twaiSummonTxFull = 0;

  canHardReinitCount = 0;
  canHardReinitFailCount = 0;
  canRecoverySleepCount = 0;
  canRecoveryWakeCount = 0;

  runtimeStatsResetCount++;
  runtimeStatsLastResetMs = (uint32_t)millis();
}

static void httpResetRuntimeStats() {
  resetRuntimeStats();
  server.send(200, "application/json", "{\"ok\":true,\"action\":\"runtime-stats-reset\"}");
}

static void webTask(void *arg) {
  Serial.println("WiFi: Starting AP...");
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(100);
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "T2CAN-%02X%02X", mac[4], mac[5]);
  while (!WiFi.softAP(ssid, "12345678")) {
    Serial.println("WiFi: Failed to start AP, retrying...");
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
  IPAddress ip = WiFi.softAPIP();
  bootCaptureMarkOnce(&bootCapWifiReadyMs);
  Serial.printf("AP: SSID=%s IP=%s\n", ssid, ip.toString().c_str());

  server.on("/",                  HTTP_GET,  httpRoot);
  server.on("/api/nag/config",    HTTP_GET,  httpNagConfig);
  server.on("/api/nag/stats",     HTTP_GET,  httpNagStats);
  server.on("/api/nag/mode",      HTTP_POST, httpNagSetMode);
  server.on("/api/nag/update",    HTTP_POST, httpNagUpdate);
  server.on("/api/nag/reset",     HTTP_POST, httpNagReset);
  server.on("/api/summon/stats",  HTTP_GET,  httpSummonStats);
  server.on("/api/summon/enable", HTTP_POST, httpSummonEnable);
  server.on("/api/summon/disable",HTTP_POST, httpSummonDisable);
  server.on("/api/summon/tlssc-enable",  HTTP_POST, httpSummonTlsscEnable);
  server.on("/api/summon/tlssc-disable", HTTP_POST, httpSummonTlsscDisable);
  server.on("/api/summon/forcemode", HTTP_POST, httpSummonForceMode);
  server.on("/api/blinkA/stats", HTTP_GET, httpBlinkAStats);
  server.on("/api/blinkA/enable", HTTP_POST, httpBlinkAEnable);
  server.on("/api/blinkA/disable", HTTP_POST, httpBlinkADisable);
  server.on("/api/blinkA/delay", HTTP_POST, httpBlinkADelay);
  server.on("/api/das/stats", HTTP_GET, httpDasTelemetryStats);
  server.on("/api/system/stats",  HTTP_GET,  httpSystemStats);
  server.on("/api/system/boot-capture.csv", HTTP_GET, httpBootCaptureCsv);
  server.on("/api/system/reset-stats", HTTP_POST, httpResetRuntimeStats);
  server.on("/api/system/reinit-can", HTTP_POST, httpCanHardReinit);
  server.on("/api/system/reboot", HTTP_POST, httpRebootT2Can);
  server.on("/update", HTTP_POST, httpOtaFinish, httpOtaUpload);
  server.begin();

  for (;;) {
    server.handleClient();
    webBeat++;
    vTaskDelay(1);
  }
}

// ═══════════════════════════════════════════════════════════════
// CAN TASKS
// ═══════════════════════════════════════════════════════════════

// Reinitialize the MCP2515 cleanly (reset + bitrate + normal mode).
// Always use the same MCP_CLOCK constant.
static void mcpReinit() {
  Can_A.reset();
  delay(2); // conservative margin beyond MCP2515 128-cycle oscillator startup
  Can_A.setBitrate(CAN_500KBPS, MCP_CLOCK);
  Can_A.setNormalMode();
  mcpTxFailConsecutive = 0;
}

static void canTaskMcp(void* arg) {
  Serial.println("[CAN A] MCP2515 task started");
  for (;;) {
    canTaskMcpHeartbeatMs = (uint32_t)millis();
    if (canTasksStopping) {
      canTaskMcpQuiesced = true;
      while (canTasksStopping) vTaskDelay(pdMS_TO_TICKS(5));
      canTaskMcpQuiesced = false;
      continue;
    }
    // ── BOUNDED READ LOOP ──
    // Never drain more than MCP_RX_BUDGET frames without yielding the
    // task. If an RX buffer gets stuck (uncleared overflow -> same
    // frame repeated in a loop), the task still exits: no more
    // infinite loop -> no freeze / watchdog.
    struct can_frame rxf;
    uint8_t budget = MCP_RX_BUDGET;
    while (budget-- && Can_A.readMessage(&rxf) == MCP2515::ERROR_OK) {
      lastCanAFrameMs = (uint32_t)millis();
      mcpRxCount++;
      bootCaptureObservePartyFrame((uint16_t)(rxf.can_id & 0x7FF), rxf.can_dlc, rxf.data);
      nagProcessMcpFrame(rxf);

#if MODEL_YL
      // rev.10: Model YL DAS_visualDebug 0x24A is on Party CAN (CAN A/MCP).
      // The same numeric ID also exists on VH CAN as a different DLC-4 frame,
      // so only the Party CAN DLC-8 frame is used as the Auto Blinker trigger.
      if (((rxf.can_id & 0x7FF) == VISUAL_DEBUG_ID) && rxf.can_dlc >= 8) {
        visualBehaviorType = (uint8_t)readBitsLE(rxf.data, 56, 2);
        visualDebugRxCount++;
        visualDebugLastMs = millis();
        evaluateAutoBlinker();
      }
#endif
    }

    // ── STATUS CHECK / RECOVERY (1 Hz) ──
    unsigned long now = millis();
    if (now - lastMcpStatusMs >= 1000) {
      lastMcpStatusMs = now;

      // Read the REAL MCP2515 error flags (EFLG register).
      uint8_t eflg = Can_A.getErrorFlags();

      // 1) RX overflow: MUST be cleared, otherwise the controller stops
      //    receiving in this buffer and the Nag Killer appears frozen.
      if (eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) {
        Can_A.clearRXnOVR();
        Serial.println("[CAN A] RX overflow flags cleared");
      }

      // 2) REAL bus-off via EFLG_TXBO (not only TX failures).
      uint8_t consecutive = mcpTxFailConsecutive;
      bool busOff = (eflg & MCP2515::EFLG_TXBO) || (consecutive > 5);

      if (busOff) {
        mcpState = 2; // BUS-OFF
        if (now - lastMcpRecoverMs > 3000) {
          lastMcpRecoverMs = now;
          Serial.printf("[CAN A] MCP2515 bus-off (eflg=0x%02X txFailSeq=%u), reset...\n",
                        eflg, consecutive);
          mcpReinit();
        }
      } else if (consecutive > 0 || (eflg & (MCP2515::EFLG_TXWAR | MCP2515::EFLG_RXWAR))) {
        mcpState = 1; // Warning
      } else {
        mcpState = 0; // OK
      }
    }

    vTaskDelay(1);
  }
}

static void canTaskTwai(void* arg) {
  Serial.println("[CAN B] TWAI task started");
  unsigned long lastTwaiStatusMs = 0;
  unsigned long lastNoCanWarn = 0;

  for (;;) {
    canTaskTwaiHeartbeatMs = (uint32_t)millis();
    if (canTasksStopping) {
      canTaskTwaiQuiesced = true;
      while (canTasksStopping) vTaskDelay(pdMS_TO_TICKS(5));
      canTaskTwaiQuiesced = false;
      continue;
    }
    twai_message_t f;
    uint8_t rxBudget = 0;
    while (rxBudget < TWAI_RX_DRAIN_BUDGET &&
           twai_receive(&f, pdMS_TO_TICKS(2)) == ESP_OK) {
      rxBudget++;
      // Keep the supervisor heartbeat alive even under sustained CAN B traffic.
      canTaskTwaiHeartbeatMs = (uint32_t)millis();
      lastCanBFrameMs = (uint32_t)millis();
      canAnyFrames++;
      canRxBeat++;
      lastCanFrameMs = millis();
      bootCaptureObserveVhFrame(f.identifier, f.data_length_code);

      switch (f.identifier) {
        case LEFTSTALK_ID:
          if (f.data_length_code >= 3) handle249OnCanB(f.data, f.data_length_code);
          break;

#if !MODEL_YL
        // Standard Model 3/Y routing: DAS_visualDebug 0x24A is handled here.
        // Model YL handles the Party-CAN DLC-8 0x24A in canTaskMcp() instead.
        case VISUAL_DEBUG_ID:
          if (f.data_length_code >= 8) {
            visualBehaviorType = (uint8_t)readBitsLE(f.data, 56, 2);
            visualDebugRxCount++;
            visualDebugLastMs = millis();
            evaluateAutoBlinker();
          }
          break;

        // Standard model : summon STATUS frames 280/390/921 are read on CAN B.
        // On Model YL these are read on CAN A (MCP2515) instead.
        case 280:
          if (f.data_length_code >= 7) handle280(f.data);
          break;
        case 390:
          if (f.data_length_code >= 8) handle390(f.data);
          break;
        case 921:
          if (f.data_length_code >= 1) handle921(f.data);
          break;
#endif
        // 1016 (SPR) is read on CAN B for both models.
        case DRIVER_ASSIST_ID:
          // 0x3F8 is RX-only: SPR detection + passive stock ULC telemetry.
          handle1016(f.data, f.data_length_code);
          break;
        case 1021:
          if (f.data_length_code >= 8) {
            uint8_t mux = readMuxID(f.data);
            if (mux == 1)      injectSummon(f);
            else if (mux == 0) injectTLSSC(f);
          }
          break;

        default:
          break;
      }
    }

    // Expire PARK_STANDBY if the confirmed gear source becomes stale.
    // SUMMON_FULL uses a short dropout grace before leaving the active session.
    refreshSummonPriorityState();
    twaiReadQueueStatus();
    blinkATxTick();

    // TWAI status / recovery. Recovery ends in STOPPED, so explicitly
    // restart the driver instead of leaving CAN B silent after BUS_OFF.
    unsigned long now = millis();
    if (now - lastTwaiStatusMs >= 1000) {
      lastTwaiStatusMs = now;
      twai_status_info_t st = {};
      if (twai_get_status_info(&st) == ESP_OK) {
        if (st.state == TWAI_STATE_RUNNING) {
          twaiReady = true;
        } else if (st.state == TWAI_STATE_BUS_OFF) {
          twaiReady = false;
          Serial.println("[CAN B] TWAI bus-off -> recovery started");
          twai_initiate_recovery();
        } else if (st.state == TWAI_STATE_STOPPED) {
          twaiReady = false;
          esp_err_t rs = twai_start();
          if (rs == ESP_OK) {
            twaiReady = true;
            Serial.println("[CAN B] TWAI recovery complete -> restarted");
          } else {
            Serial.printf("[CAN B] TWAI restart failed: %s\n", esp_err_to_name(rs));
          }
        } else {
          twaiReady = false;
        }
      }
    }

    // No-CAN warning (shared counter)
    if ((millis() - bootTime) > 20000 && canAnyFrames == 0) {
      if (millis() - lastNoCanWarn > 5000) {
        Serial.println("No CAN frames yet on either bus, staying alive.");
        lastNoCanWarn = millis();
      }
    }

    // Summon watchdog: if CAN 280 silent > PARKED_TIMEOUT_MS
    uint32_t nowMs = (uint32_t)millis();
    portENTER_CRITICAL(&stateMux);
    bool can280Stale = (last280Millis > 0) && (nowMs - last280Millis > PARKED_TIMEOUT_MS);
    if (can280Stale) gateParked = true;
    portEXIT_CRITICAL(&stateMux);

    vTaskDelay(1);
  }
}


// ═══════════════════════════════════════════════════════════════
// RECOVERY-ONLY CAN SUBSYSTEM SUPERVISOR
// ═══════════════════════════════════════════════════════════════

static inline bool recoveryFresh(uint32_t now, uint32_t ts, uint32_t timeoutMs) {
  return ts != 0 && (uint32_t)(now - ts) <= timeoutMs;
}

static void requestCanSubsystemRestart(uint8_t reason) {
  portENTER_CRITICAL(&canRecoveryMux);
  if (reason > canSupervisorCommand) canSupervisorCommand = reason;
  portEXIT_CRITICAL(&canRecoveryMux);
}

static bool recoveryMcpColdInit() {
  mcpReady = false;
  if (mcpSpiStarted) {
    SPI.end();
    mcpSpiStarted = false;
    delay(20);
  }

  pinMode(MCP2515_CS, OUTPUT);
  digitalWrite(MCP2515_CS, HIGH);
  pinMode(MCP2515_RST, OUTPUT);
  digitalWrite(MCP2515_RST, HIGH);
  delay(1);
  digitalWrite(MCP2515_RST, LOW);
  delay(2);
  digitalWrite(MCP2515_RST, HIGH);
  delay(2);

  SPI.begin(MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI, MCP2515_CS);
  mcpSpiStarted = true;
  delay(20);

  Can_A.reset();
  delay(2);
  MCP2515::ERROR rateErr = Can_A.setBitrate(CAN_500KBPS, MCP_CLOCK);
  MCP2515::ERROR modeErr = (rateErr == MCP2515::ERROR_OK) ? Can_A.setNormalMode() : rateErr;
  bool ok = (rateErr == MCP2515::ERROR_OK && modeErr == MCP2515::ERROR_OK);
  mcpReady = ok;
  if (ok) {
    mcpTxFailConsecutive = 0;
    mcpState = 0;
  } else {
    mcpState = 2;
    Serial.printf("[CAN A] cold init failed: bitrate=%d mode=%d\n", (int)rateErr, (int)modeErr);
  }
  return ok;
}

static bool recoveryTwaiInstallFresh() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
  g.rx_queue_len = 256;
  g.tx_queue_len = TWAI_TX_QUEUE_LEN;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t ie = twai_driver_install(&g, &t, &f);
  if (ie != ESP_OK) {
    twaiReady = false;
    Serial.printf("[CAN B] fresh install failed: %s\n", esp_err_to_name(ie));
    return false;
  }
  esp_err_t se = twai_start();
  if (se != ESP_OK) {
    twaiReady = false;
    Serial.printf("[CAN B] fresh start failed: %s\n", esp_err_to_name(se));
    twai_driver_uninstall();
    return false;
  }
  uint32_t alerts = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS |
                    TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS |
                    TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_DATA |
                    TWAI_ALERT_RX_QUEUE_FULL;
  twai_reconfigure_alerts(alerts, NULL);
  twaiReady = true;
  return true;
}

static bool recoveryTwaiFullReinit() {
  twaiReady = false;
  twai_status_info_t st = {};
  esp_err_t gs = twai_get_status_info(&st);

  if (gs == ESP_OK) {
    if (st.state == TWAI_STATE_RUNNING) {
      esp_err_t e = twai_stop();
      if (e != ESP_OK) {
        Serial.printf("[CAN B] hard stop failed: %s\n", esp_err_to_name(e));
        return false;
      }
    } else if (st.state == TWAI_STATE_BUS_OFF || st.state == TWAI_STATE_RECOVERING) {
      if (st.state == TWAI_STATE_BUS_OFF) {
        esp_err_t e = twai_initiate_recovery();
        if (e != ESP_OK) {
          Serial.printf("[CAN B] hard recovery start failed: %s\n", esp_err_to_name(e));
          return false;
        }
      }
      uint32_t start = (uint32_t)millis();
      while ((uint32_t)((uint32_t)millis() - start) < RECOVERY_TWAI_WAIT_MS) {
        twai_status_info_t cur = {};
        if (twai_get_status_info(&cur) != ESP_OK) break;
        if (cur.state == TWAI_STATE_STOPPED) break;
        delay(25);
      }
      twai_status_info_t cur = {};
      if (twai_get_status_info(&cur) == ESP_OK && cur.state != TWAI_STATE_STOPPED) {
        Serial.println("[CAN B] recovery did not reach STOPPED");
        return false;
      }
    }
  }

  esp_err_t ue = twai_driver_uninstall();
  if (ue != ESP_OK && ue != ESP_ERR_INVALID_STATE) {
    Serial.printf("[CAN B] uninstall failed: %s\n", esp_err_to_name(ue));
    return false;
  }

  pinMode(CAN_TX, INPUT);
  pinMode(CAN_RX, INPUT);
  delay(50);
  return recoveryTwaiInstallFresh();
}

static void recoveryStopCanTasks() {
  canTasksStopping = true;
  canTaskMcpQuiesced = false;
  canTaskTwaiQuiesced = false;

  uint32_t start = (uint32_t)millis();
  while ((!canTaskMcpQuiesced || !canTaskTwaiQuiesced) &&
         (uint32_t)((uint32_t)millis() - start) < 300) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  TaskHandle_t a = canTaskMcpHandle;
  TaskHandle_t b = canTaskTwaiHandle;
  canTaskMcpHandle = nullptr;
  canTaskTwaiHandle = nullptr;
  if (a) vTaskDelete(a);
  if (b) vTaskDelete(b);

  canTasksStopping = false;
  canTaskMcpQuiesced = false;
  canTaskTwaiQuiesced = false;
  canTaskMcpHeartbeatMs = 0;
  canTaskTwaiHeartbeatMs = 0;
  vTaskDelay(pdMS_TO_TICKS(RECOVERY_TASK_STOP_SETTLE_MS));
}

static bool recoveryStartCanTasks() {
  BaseType_t a = xTaskCreatePinnedToCore(canTaskMcp, "canA", 8192, nullptr, 5, &canTaskMcpHandle, 1);
  if (a != pdPASS) {
    canTaskMcpHandle = nullptr;
    return false;
  }
  BaseType_t b = xTaskCreatePinnedToCore(canTaskTwai, "canB", 8192, nullptr, 4, &canTaskTwaiHandle, 1);
  if (b != pdPASS) {
    vTaskDelete(canTaskMcpHandle);
    canTaskMcpHandle = nullptr;
    canTaskTwaiHandle = nullptr;
    return false;
  }
  return true;
}

static bool recoveryHardReinitialize(uint8_t reason) {
  if (canSubsystemBusy) return false;
  canSubsystemBusy = true;
  const int8_t bootCapHardIdx = bootCaptureHardStart(reason);
  canLastHardReinitReason = reason;
  canHardReinitCount++;
  Serial.printf("[CAN SUP] hard CAN reinitialize #%lu reason=%u\n",
                (unsigned long)canHardReinitCount, (unsigned)reason);

  recoveryStopCanTasks();
  // Fail closed immediately during CAN recovery; a previously latched state=5
  // must never authorize a new Auto Blinker request after bus interruption.
  portENTER_CRITICAL(&stateMux);
  lastDASStatusMillis = 0;
  portEXIT_CRITICAL(&stateMux);
  bool aOk = recoveryMcpColdInit();
  bool bOk = recoveryTwaiFullReinit();
  bool tasksOk = aOk && bOk && recoveryStartCanTasks();

  lastCanAFrameMs = 0;
  lastCanBFrameMs = 0;
  canInitTime = millis();
  recoveryOneBusStaleStartMs = 0;
  recoveryWakeAcquireStartMs = (reason == CAN_SUP_HARD_ACQUIRE || recoveryEverBothActive)
                                 ? (uint32_t)millis() : 0;
  canSubsystemBusy = false;

  if (!(aOk && bOk && tasksOk)) {
    bootCaptureHardFinish(bootCapHardIdx, false);
    canHardReinitFailCount++;
    Serial.printf("[CAN SUP] hard CAN reinitialize FAILED A=%u B=%u tasks=%u\n",
                  aOk ? 1 : 0, bOk ? 1 : 0, tasksOk ? 1 : 0);
    return false;
  }
  bootCaptureHardFinish(bootCapHardIdx, true);
  Serial.println("[CAN SUP] hard CAN reinitialize complete");
  return true;
}

static void canSupervisorTask(void* arg) {
  Serial.println("[CAN SUP] recovery-only supervisor started");
  for (;;) {
    uint32_t now = (uint32_t)millis();

    if (!canSubsystemBusy) {
      // Independent task heartbeat: still advances while the vehicle is asleep.
      // Therefore silence on the CAN wires is not confused with a wedged task.
      bool graceDone = (uint32_t)(now - canInitTime) >= RECOVERY_TASK_START_GRACE_MS;
      bool aTaskDead = canTaskMcpHandle && graceDone &&
                       (canTaskMcpHeartbeatMs == 0 ||
                        (uint32_t)(now - canTaskMcpHeartbeatMs) > RECOVERY_TASK_HEARTBEAT_TIMEOUT_MS);
      bool bTaskDead = canTaskTwaiHandle && graceDone &&
                       (canTaskTwaiHeartbeatMs == 0 ||
                        (uint32_t)(now - canTaskTwaiHeartbeatMs) > RECOVERY_TASK_HEARTBEAT_TIMEOUT_MS);
      if (aTaskDead || bTaskDead) {
        Serial.printf("[CAN SUP] task heartbeat stale A=%u B=%u\n", aTaskDead ? 1 : 0, bTaskDead ? 1 : 0);
        requestCanSubsystemRestart(CAN_SUP_HARD_STALE);
      }

      bool aFresh = recoveryFresh(now, lastCanAFrameMs, RECOVERY_BUS_FRESH_MS);
      bool bFresh = recoveryFresh(now, lastCanBFrameMs, RECOVERY_BUS_FRESH_MS);
      bool bothFresh = aFresh && bFresh;
      bool anyFresh = aFresh || bFresh;

      if (bothFresh) {
        if (!recoveryEverBothActive || recoverySleeping || recoveryWakeAcquireStartMs != 0) {
          Serial.println("[CAN SUP] CAN A+B active");
        }
        recoveryEverBothActive = true;
        recoverySleeping = false;
        recoveryWakeAcquireStartMs = 0;
        recoveryOneBusStaleStartMs = 0;
        recoveryLastBothActiveMs = now;
        recoveryColdRetryCount = 0;
        recoveryColdRetriesExhausted = false;
      } else if (recoveryEverBothActive) {
        // Once a real active session has been observed, both buses going quiet
        // together is treated as normal Tesla sleep, never as a CAN failure.
        if (!anyFresh) {
          recoveryOneBusStaleStartMs = 0;
          if (!recoverySleeping && recoveryLastBothActiveMs != 0 &&
              (uint32_t)(now - recoveryLastBothActiveMs) >= RECOVERY_SLEEP_QUIET_MS) {
            recoverySleeping = true;
            recoveryWakeAcquireStartMs = 0;
            canRecoverySleepCount++;
            Serial.printf("[CAN SUP] vehicle CAN sleep #%lu -> passive wait\n",
                          (unsigned long)canRecoverySleepCount);
          }
        } else {
          if (recoverySleeping) {
            recoverySleeping = false;
            recoveryWakeAcquireStartMs = now;
            canRecoveryWakeCount++;
            Serial.printf("[CAN SUP] vehicle CAN wake #%lu -> acquire other bus\n",
                          (unsigned long)canRecoveryWakeCount);
          }

          if (recoveryWakeAcquireStartMs == 0) {
            if (recoveryOneBusStaleStartMs == 0) recoveryOneBusStaleStartMs = now;
            if ((uint32_t)(now - recoveryOneBusStaleStartMs) >= RECOVERY_ONE_BUS_STALE_MS &&
                (recoveryLastHardRequestMs == 0 ||
                 (uint32_t)(now - recoveryLastHardRequestMs) >= RECOVERY_HARD_COOLDOWN_MS)) {
              recoveryLastHardRequestMs = now;
              recoveryOneBusStaleStartMs = 0;
              requestCanSubsystemRestart(CAN_SUP_HARD_STALE);
            }
          }
        }

        if (!recoverySleeping && recoveryWakeAcquireStartMs != 0 &&
            (uint32_t)(now - recoveryWakeAcquireStartMs) >= RECOVERY_WAKE_ACQUIRE_MS &&
            (recoveryLastHardRequestMs == 0 ||
             (uint32_t)(now - recoveryLastHardRequestMs) >= RECOVERY_HARD_COOLDOWN_MS)) {
          recoveryLastHardRequestMs = now;
          recoveryWakeAcquireStartMs = now;
          requestCanSubsystemRestart(CAN_SUP_HARD_ACQUIRE);
        }
      } else {
        // Cold boot / T-2CAN reset before a full A+B acquisition.
        // Retry hard initialization only a bounded number of times. If the
        // vehicle is simply asleep, stop tearing controllers down and wait for
        // a real CAN frame to wake the acquisition path.
        if (anyFresh && recoveryWakeAcquireStartMs == 0) recoveryWakeAcquireStartMs = now;

        uint32_t sinceInit = (uint32_t)(now - canInitTime);
        bool acquisitionTimedOut = (recoveryWakeAcquireStartMs != 0)
          ? ((uint32_t)(now - recoveryWakeAcquireStartMs) >= RECOVERY_COLD_FIRST_ACQUIRE_MS)
          : (sinceInit >= RECOVERY_COLD_FIRST_ACQUIRE_MS);

        uint32_t interval = (recoveryColdRetryCount == 0)
          ? RECOVERY_COLD_FIRST_ACQUIRE_MS : RECOVERY_COLD_RETRY_INTERVAL_MS;
        bool intervalPassed = (recoveryLastHardRequestMs == 0) ||
                              ((uint32_t)(now - recoveryLastHardRequestMs) >= interval);

        if (acquisitionTimedOut && intervalPassed &&
            recoveryColdRetryCount < RECOVERY_COLD_MAX_RETRIES) {
          recoveryColdRetryCount++;
          recoveryLastHardRequestMs = now;
          recoveryWakeAcquireStartMs = 0;
          Serial.printf("[CAN SUP] cold acquire retry %u/%u\n",
                        (unsigned)recoveryColdRetryCount,
                        (unsigned)RECOVERY_COLD_MAX_RETRIES);
          requestCanSubsystemRestart(CAN_SUP_HARD_ACQUIRE);
        } else if (recoveryColdRetryCount >= RECOVERY_COLD_MAX_RETRIES &&
                   !recoveryColdRetriesExhausted) {
          recoveryColdRetriesExhausted = true;
          recoverySleeping = true;
          Serial.println("[CAN SUP] no RX after bounded retries -> passive sleep/wake wait");
        }

        // If a real frame arrives after passive wait, resume bounded acquisition.
        if (recoveryColdRetriesExhausted && anyFresh) {
          recoveryColdRetriesExhausted = false;
          recoverySleeping = false;
          recoveryColdRetryCount = 0;
          recoveryWakeAcquireStartMs = now;
        }
      }
    }

    uint8_t cmd = CAN_SUP_NONE;
    portENTER_CRITICAL(&canRecoveryMux);
    cmd = canSupervisorCommand;
    canSupervisorCommand = CAN_SUP_NONE;
    portEXIT_CRITICAL(&canRecoveryMux);

    if (cmd != CAN_SUP_NONE && !canSubsystemBusy) {
      if (!recoveryHardReinitialize(cmd)) {
        Serial.println("[CAN SUP] subsystem recovery failed -> reboot T-2CAN");
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ═══════════════════════════════════════════════════════════════
// SETUP / LOOP
// ═══════════════════════════════════════════════════════════════

void setup() {
  bootTime = millis();
  Serial.begin(115200);
  delay(100); // rev.14: serial settle only; CAN startup is not held here

  rtcBootCount++;
  esp_reset_reason_t reset_reason = esp_reset_reason();
  Serial.printf("\n=== T2CAN Unified BOOT ===\n");
  Serial.printf("Reset reason: %d (%s)\n", reset_reason, resetReasonName(reset_reason));
  Serial.printf("RTC boot count: %lu\n", (unsigned long)rtcBootCount);
  if (reset_reason == ESP_RST_BROWNOUT) {
    Serial.println("WARNING: Brownout detected!");
  }
  Serial.printf("IDF version: %s\n", esp_get_idf_version());

  // NVS init
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("NVS: Corrupted, erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    Serial.printf("NVS: Init failed %d\n", err);
  }

  // Load configs
  nagCfgLoad();
  summonCfgLoad();

  Serial.printf("Nag mode=%u id=0x%03X torqueCount=%u enabled=%u\n",
    nagCfg.mode, nagCfg.targetId, nagCfg.torqueCount, nagCfg.enabled);
  Serial.printf("Summon enabled=%s\n", summonEnabled ? "true" : "false");
  Serial.printf("TLSSC enabled=%s (0x3FD mux0 bit38)\n", tlsscEnabled ? "true" : "false");

  // rev.14: board power-on is treated as the wake signal for RX.
  // Existing per-feature validity gates still control every injection/TX path.
  Serial.println("Driver-wake power detected. Starting CAN init immediately...");

  // ══ Init CAN A (MCP2515) ══
  Serial.println("[CAN A] Initializing MCP2515...");
  pinMode(MCP2515_RST, OUTPUT);
  digitalWrite(MCP2515_RST, HIGH);
  delay(1);
  digitalWrite(MCP2515_RST, LOW);
  delay(2);
  digitalWrite(MCP2515_RST, HIGH);
  delay(2);

  SPI.begin(MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI, MCP2515_CS);
  mcpSpiStarted = true;

  Can_A.reset();
  delay(2); // conservative margin beyond MCP2515 128-cycle oscillator startup
  Can_A.setBitrate(CAN_500KBPS, MCP_CLOCK);
  Can_A.setNormalMode();
  mcpReady = true;
  Serial.printf("[CAN A] MCP2515 ready (500 kbps, clk=%s)\n",
                (MCP_CLOCK == MCP_16MHZ) ? "16MHz" :
                (MCP_CLOCK == MCP_8MHZ)  ? "8MHz" : "20MHz");

  // ══ Init CAN B (TWAI) ══
  Serial.println("[CAN B] Initializing TWAI...");
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      (gpio_num_t)CAN_TX, (gpio_num_t)CAN_RX, TWAI_MODE_NORMAL);
  g.rx_queue_len = 256;
  g.tx_queue_len = TWAI_TX_QUEUE_LEN;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err1 = twai_driver_install(&g, &t, &f);
  esp_err_t err2 = twai_start();
  Serial.printf("[CAN B] TWAI: %s / %s\n", esp_err_to_name(err1), esp_err_to_name(err2));

  if (err1 != ESP_OK || err2 != ESP_OK) {
    Serial.println("[CAN B] TWAI init failed! Rebooting...");
    delay(3000);
    ESP.restart();
  }

  uint32_t alerts_to_enable = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS |
                              TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS |
                              TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_DATA |
                              TWAI_ALERT_RX_QUEUE_FULL;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
    Serial.println("[CAN B] TWAI alerts configured");
  }

  canInitTime = millis();
  twaiReady = true;
  bootCaptureMarkOnce(&bootCapCanInitDoneMs);

  // Start CAN tasks immediately after both controllers are ready/running.
  BaseType_t retMcp = xTaskCreatePinnedToCore(canTaskMcp, "canA", 8192, nullptr, 5, &canTaskMcpHandle, 1);
  if (retMcp != pdPASS) {
    Serial.printf("CAN A task creation failed: %d\n", retMcp);
    delay(3000);
    ESP.restart();
  }

  BaseType_t retTwai = xTaskCreatePinnedToCore(canTaskTwai, "canB", 8192, nullptr, 4, &canTaskTwaiHandle, 1);
  if (retTwai != pdPASS) {
    Serial.printf("CAN B task creation failed: %d\n", retTwai);
    delay(3000);
    ESP.restart();
  }
  bootCaptureMarkOnce(&bootCapCanTasksStartedMs);

  BaseType_t retSup = xTaskCreatePinnedToCore(canSupervisorTask, "canSup", 6144, nullptr, 3, &canSupervisorHandle, 0);
  if (retSup != pdPASS) {
    Serial.printf("CAN supervisor task creation failed: %d\n", retSup);
    delay(3000);
    ESP.restart();
  }

  Serial.printf("[BOOT] CAN RX tasks started at %lu ms\n", (unsigned long)(millis() - bootTime));

  // Start Wi-Fi/web after the CAN receive path is live.
  BaseType_t retWeb = xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);
  if (retWeb != pdPASS) {
    Serial.printf("Web task creation failed: %d\n", retWeb);
    delay(3000);
    ESP.restart();
  }

  Serial.println("BOOT OK");
}

void loop() {
  static unsigned long lastBeatLog = 0;
  static uint32_t loopBeat = 0;
  loopBeat++;
  unsigned long now = millis();

  if (now - lastBeatLog >= 5000) {
    lastBeatLog = now;
    unsigned long canAgeMs = (lastCanFrameMs == 0) ? 999999 : (now - lastCanFrameMs);
    Serial.printf(
      "[BEAT] uptime=%lu loop=%lu canBeat=%lu canRxBeat=%lu webBeat=%lu canFrames=%lu canAgeMs=%lu mcpTxOk=%lu mcpTxFail=%lu sumTxOk=%lu sumTxFail=%lu heap=%u\n",
      now / 1000,
      (unsigned long)loopBeat,
      (unsigned long)canBeat,
      (unsigned long)canRxBeat,
      (unsigned long)webBeat,
      (unsigned long)canAnyFrames,
      canAgeMs,
      (unsigned long)mcpTxOk,
      (unsigned long)mcpTxFail,
      (unsigned long)sumTxOk,
      (unsigned long)sumTxFail,
      ESP.getFreeHeap()
    );
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
}
