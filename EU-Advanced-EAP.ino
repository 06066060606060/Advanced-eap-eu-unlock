// T2CAN Unified - Dual CAN (MCP2515 + TWAI) for LilyGo T-2CAN
// CAN A (MCP2515) -> Advanced EAP: TX SCCM_leftStalk (585/0x249) + RX SCCM_leftStalk for alignment
// CAN B (TWAI)    -> Summon Unlock (IDs 280, 390, 921, 1016, 1021) ; 921 = AP status gate

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <Update.h>
#include "driver/twai.h"
#include "index_html.h"

#define FW_VERSION "T2CAN-V1.0b-ADV-EAP"

// T-2CAN board specific
#include "pin_config.h"
#include <mcp2515.h>
#include <SPI.h>

static unsigned long bootTime = 0;
static unsigned long canInitTime = 0;
static volatile bool twaiReady = false;
static volatile bool mcpReady = false;
static volatile uint32_t canAnyFrames = 0;
static volatile unsigned long lastCanFrameMs = 0;
static volatile uint32_t canBeat = 0;
static volatile uint32_t canRxBeat = 0;
static volatile uint32_t webBeat = 0;
RTC_DATA_ATTR uint32_t rtcBootCount = 0;
static Preferences prefs;

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
// ADVANCED EAP (CAN A - MCP2515)
//   TX : SCCM_leftStalk (585 / 0x249) -> auto blinker + manual control
//   RX : SCCM_leftStalk (585 / 0x249) -> counter/checksum alignment
// ═══════════════════════════════════════════════════════════════

static const unsigned long DRIVER_WAKE_DELAY_MS = 10000;

// Diagnostics for the real SCCM_leftStalk frame received on CAN A.
#define LEFTSTALK_ID 0x249
static volatile uint32_t rx249 = 0;
static volatile uint8_t realCounter = 0;
static volatile uint8_t realTurn = 0;
static volatile uint8_t realCksum = 0;
static volatile bool cksumSelfTest = true;
static volatile bool seen249 = false;
static portMUX_TYPE blinkAMux = portMUX_INITIALIZER_UNLOCKED;

static inline uint32_t readBitsLE(const uint8_t *data, int startBit, int len);
static void handle249OnCanA(const uint8_t *data, uint8_t dlc);
static void evaluateAutoBlinkerA();

static void eapProcessMcpFrame(const struct can_frame& rxf) {
  uint16_t id = rxf.can_id & 0x7FF;
  if (id == LEFTSTALK_ID) {
    handle249OnCanA(rxf.data, rxf.can_dlc);
  }
}

// ═══════════════════════════════════════════════════════════════
// DAS_visualDebug (0x24A / 586) - CAN B
//   behaviorType = bit 56/2
// ═══════════════════════════════════════════════════════════════
#define VISUAL_DEBUG_ID 0x24A
static volatile uint8_t visualBehaviorType = 0;
static volatile uint32_t visualDebugRxCount = 0;
static volatile uint32_t visualDebugLastMs = 0;

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

// ── Auto Blinker: direction read from DAS_behaviorType on CAN B ──
// Triggered by DAS_visualDebug.behaviorType (0x24A, bit 56/2 bits):
//   2 = LANE_CHANGE_LEFT, 3 = LANE_CHANGE_RIGHT.
// Autopilot active or Force Mode must be active. The trigger is delayed
// by the configurable auto-blinker delay before the one-shot is sent.
// The manual override remains available.

static inline uint32_t readBitsLE(const uint8_t *data, int startBit, int len) {
  uint32_t val = 0;
  for (int i = 0; i < len; i++) {
    int totalBit = startBit + i;
    int byteIdx  = totalBit / 8;
    int bitIdx   = totalBit % 8;
    if ((data[byteIdx] >> bitIdx) & 0x01) val |= (1UL << i);
  }
  return val;
}

#define STALK_IDLE   0
#define STALK_UP_1   2
#define STALK_DOWN_1 7

#define BLINKA_TX_PERIOD_MS 20
#define BLINKA_PULSE_MS 350
#define BLINKA_AUTO_DELAY_DEFAULT_MS 3000

// ── SCCM_leftStalk checksum (585 / 0x249) ──
// Reverse-engineered from real CAN logs: this is NOT a CRC.
// Evidence: counters 3 and 4 both produce 0xD3 (7 and 9 -> 0x5E),
// which is impossible for a CRC-8. The separable model was verified
// on 92 frames with zero errors:
//     checksum = CKSUM_CTR[counter] XOR turnLinear(turn)
// CKSUM_CTR: fixed table indexed by the counter (0..15), fully observed.
// turnLinear: linear XOR contribution for each bit of the turn nibble.
// NOTE: turn=2 (UP_1) and turn=4 (UP_2) are verified by the log.
// turn=7 (DOWN_1) and turn=8 (DOWN_2) are extrapolated (no DOWN frame was captured).
static const uint8_t CKSUM_CTR[16] = {
  0x9B, 0xE8, 0x2A, 0xD3, 0xD3, 0x83, 0x4C, 0x5E,
  0x3F, 0x5E, 0xE2, 0x28, 0x3A, 0x13, 0xAF, 0xCE
};
static inline uint8_t turnLinear(uint8_t turn) {
  uint8_t r = 0;
  if (turn & 0x01) r ^= 0x0E;
  if (turn & 0x02) r ^= 0x1C;
  if (turn & 0x04) r ^= 0x38;
  if (turn & 0x08) r ^= 0x70;
  return r;
}
static inline uint8_t leftStalkChecksum(uint8_t counter, uint8_t turn) {
  return CKSUM_CTR[counter & 0x0F] ^ turnLinear(turn & 0x0F);
}
// Rolling counter 0..15 for injected frames.
static volatile uint8_t blinkACounter = 0;

static volatile bool forceMode = false;
static portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool summonEnabled = true;
static volatile bool tlsscEnabled  = false;   // "Enable TLSSC" - off by default
static volatile bool gateAPActive  = false;
static volatile bool gateParked    = true;
static volatile bool gateSummoning = false;
static volatile bool sprSeen  = false;
static volatile bool lastAca  = false;
#define PARKED_TIMEOUT_MS  5000
static volatile uint32_t last280Millis = 0;

static volatile uint32_t sumRxMux1   = 0;
static volatile uint32_t sumTxOk     = 0;
static volatile uint32_t sumTxFail   = 0;
static volatile uint32_t sumRx280    = 0;
static volatile uint32_t sumRx390    = 0;
static volatile uint32_t sumRx921    = 0;
static volatile uint32_t sumRx1016   = 0;
static char gateBlockReason[48] = "boot";

static volatile bool    blinkAEnabled           = true;
static volatile uint8_t blinkerReqA             = 0;   // 0=NONE 1=LEFT 2=RIGHT (current requested state)
static volatile uint8_t manualReq               = 0;   // 0=AUTO 1=LEFT 2=RIGHT (dashboard manual override)
static volatile uint8_t activeTurn              = STALK_IDLE; // Turn value currently transmitted on 0x249
static volatile uint8_t lastReqDir              = 0;   // Last behavior-derived direction (edge detection)
static volatile uint8_t oneShotTurn             = STALK_IDLE; // turn value transmitted during the pulse
static volatile uint32_t oneShotUntil           = 0;   // millis() when the pulse ends
// ── Auto-blinker delay: arm a delayed trigger after behavior detection ──
static volatile uint32_t blinkADelayMs          = BLINKA_AUTO_DELAY_DEFAULT_MS; // configurable delay
static volatile uint8_t  autoPendingDir         = 0;   // Delayed trigger direction waiting to fire (0=none)
static volatile uint32_t autoFireAt             = 0;   // millis() timestamp for the delayed trigger
static volatile bool     autoArmed             = false; // a delayed trigger is armed
static volatile uint32_t blkATxOk               = 0;
static volatile uint32_t blkATxFail             = 0;

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


static inline bool injectionGateOpen() {
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
    last280Millis = (uint32_t)millis();
    uint8_t gear = readDIGear(data);
    int     gs   = gearState(gear);
    portENTER_CRITICAL(&stateMux);
    if (gs == 1)  gateParked = true;
    if (gs == 0)  gateParked = false;
    bool aca = (data[6] & 0x04) != 0;
    if (lastAca && !aca)
        sprSeen = false;
    lastAca = aca;
    recomputeSummoning();
    clearSummonOnParkIfAcaInactive(gear);
    portEXIT_CRITICAL(&stateMux);
}

static void handle390(const uint8_t *data) {
    sumRx390++;
    uint8_t gear = readVehicleGear(data);
    int     gs   = gearState(gear);
    if (gs < 0) return;
    portENTER_CRITICAL(&stateMux);
    uint32_t age = (uint32_t)millis() - last280Millis;
    if (last280Millis == 0 || age > PARKED_TIMEOUT_MS) {
        gateParked = (gs == 1);
        clearSummonOnParkIfAcaInactive(gear);
    }
    portEXIT_CRITICAL(&stateMux);
}

// Convert a direction (1=LEFT, 2=RIGHT) to the SCCM turn value.
// Always use soft UP_1/DOWN_1: this is the native lane-change three-blink behavior.
// This produces the native three-blink behavior from one pulse.
static inline uint8_t dirToTurn(uint8_t dir) {
  if (dir == 1) return STALK_DOWN_1; // left
  if (dir == 2) return STALK_UP_1;   // right
  return STALK_IDLE;
}

// Process 0x249 received on CAN A (the real SCCM). Align our counter with it
// when we are not injecting, and verify our checksum formula (self-test).
// Based on turn_indicator.ino (handle249) to avoid a counter discontinuity.
static void handle249OnCanA(const uint8_t *data, uint8_t dlc) {
  if (dlc < 3) return;
  uint8_t cnt  = data[1] & 0x0F;
  uint8_t turn = data[2] & 0x0F;
  uint8_t ck   = data[0];
  uint8_t pred = leftStalkChecksum(cnt, turn);

  portENTER_CRITICAL(&blinkAMux);
  rx249++;
  realCounter   = cnt;
  realTurn      = turn;
  realCksum     = ck;
  cksumSelfTest = (pred == ck);
  seen249       = true;
  // While not injecting, follow the SCCM counter so we resume
  // the sequence cleanly when transmission starts.
  if (activeTurn == STALK_IDLE) blinkACounter = cnt;
  portEXIT_CRITICAL(&blinkAMux);
}

// Send one SCCM_leftStalk (585 / 0x249) frame on CAN A with the given turn value.
// Increment the counter before use, matching turn_indicator.ino.
static void sendStalkFrame(uint8_t turn) {
  uint8_t cnt;
  portENTER_CRITICAL(&blinkAMux);
  cnt = (blinkACounter + 1) & 0x0F;
  blinkACounter = cnt;
  portEXIT_CRITICAL(&blinkAMux);
  turn &= 0x0F;

  struct can_frame out;
  out.can_id  = 0x249;   // SCCM_leftStalk (585)
  out.can_dlc = 4;       // Real frame length = 4 bytes
  out.data[0] = leftStalkChecksum(cnt, turn); // SCCM_leftStalkChecksum (retro-engine)
  out.data[1] = cnt & 0x0F;                   // counter only, high nibble 0
  out.data[2] = turn & 0x0F;                  // turn only, reserved nibble 0
  out.data[3] = 0;
  out.data[4] = out.data[5] = out.data[6] = out.data[7] = 0;

  MCP2515::ERROR err = Can_A.sendMessage(&out);
  portENTER_CRITICAL(&blinkAMux);
  if (err == MCP2515::ERROR_OK) { blkATxOk++; mcpTxOk++; mcpTxFailConsecutive = 0; }
  else                          { blkATxFail++; mcpTxFail++; if (mcpTxFailConsecutive < 255) mcpTxFailConsecutive++; }
  portEXIT_CRITICAL(&blinkAMux);
}

// Evaluate the auto-blinker condition and arm a delayed trigger.
// The direction source is DAS_behaviorType on CAN B.
static void evaluateAutoBlinkerA() {
  bool en, ap, fmode;
  uint8_t behavior;

  portENTER_CRITICAL(&blinkAMux);
  en = blinkAEnabled;
  portEXIT_CRITICAL(&blinkAMux);

  portENTER_CRITICAL(&stateMux);
  ap = gateAPActive;
  fmode = forceMode;
  portEXIT_CRITICAL(&stateMux);

  behavior = visualBehaviorType;

  uint8_t reqDir = 0;
  if (en && (ap || fmode)) {
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

static void blinkATxTick() {
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  uint8_t turn = STALK_IDLE;
  portENTER_CRITICAL(&blinkAMux);
  // 1) Delayed auto trigger reached its deadline -> start the one-shot pulse.
  if (autoArmed && (int32_t)(now - autoFireAt) >= 0) {
    oneShotTurn  = dirToTurn(autoPendingDir);
    oneShotUntil = now + BLINKA_PULSE_MS;
    autoArmed      = false;
    autoPendingDir = 0;
  }
  // 2) Is a one-shot pulse currently active?
  if ((int32_t)(oneShotUntil - now) > 0) {
    turn = oneShotTurn;          // pulse in progress
  } else {
    oneShotTurn = STALK_IDLE;    // pulse window expired -> idle
  }
  activeTurn = turn;             // for dashboard display
  portEXIT_CRITICAL(&blinkAMux);
  if (turn == STALK_IDLE) return;
  if (now - lastMs < BLINKA_TX_PERIOD_MS) return;
  lastMs = now;
  sendStalkFrame(turn);
}

static void handle921(const uint8_t *data) {
    sumRx921++;
    bool ap = isDASActive(readDASStatus(data));
    portENTER_CRITICAL(&stateMux);
    gateAPActive = ap;
    portEXIT_CRITICAL(&stateMux);
}

static void handle1016(const uint8_t *data, uint8_t dlc) {
    if (dlc < 4) return;
    sumRx1016++;
    uint8_t spr = (data[3] >> 4) & 0x0F;
    portENTER_CRITICAL(&stateMux);
    if (spr != 0)
        sprSeen = true;
    recomputeSummoning();
    portEXIT_CRITICAL(&stateMux);
}

static void injectSummon(const twai_message_t &src) {
     bool en, gate, fmode;
    portENTER_CRITICAL(&stateMux);
    en   = summonEnabled;
    gate = injectionGateOpen();
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
    setBit(out.data, 19, false);
    setBit(out.data, 47, true);
    sumRxMux1++;
    esp_err_t err = twai_transmit(&out, pdMS_TO_TICKS(2));
    if (err == ESP_OK) sumTxOk++;
    else               sumTxFail++;
}

// ── TLSSC : 0x3FD mux0 bit38 -> UI_fsdStopsControlEnabled = 1 ──
static void injectTLSSC(const twai_message_t &src) {
    bool en, gate, fmode;
    portENTER_CRITICAL(&stateMux);
    en    = tlsscEnabled;
    gate  = injectionGateOpen();
    fmode = forceMode;
    portEXIT_CRITICAL(&stateMux);

    if ((!en || !gate) && !fmode)
        return;

    twai_message_t out;
    out.identifier       = src.identifier;
    out.data_length_code = src.data_length_code;
    out.flags            = 0;
    for (int i = 0; i < 8; i++) out.data[i] = src.data[i];
    setBit(out.data, 38, true);   // UI_fsdStopsControlEnabled = 1
    setBit(out.data, 39, true);    // UI_fsdContinueOnGreenWithCIPV = 1
	
    esp_err_t err = twai_transmit(&out, pdMS_TO_TICKS(2));
    if (err == ESP_OK) sumTxOk++;
    else               sumTxFail++;
}

static void summonCfgLoad() {
    prefs.begin("summon", true);
    summonEnabled  = prefs.getBool("en", true);
    tlsscEnabled   = prefs.getBool("tlssc", false);
    blinkAEnabled  = prefs.getBool("blkA", true);
    blinkADelayMs  = prefs.getUInt("blkADly", BLINKA_AUTO_DELAY_DEFAULT_MS);
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

static String summonStatsToJson() {
    bool en, tlssc, ap, parked, summon, aca, spr, fmode;
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
    rmx    = sumRxMux1;
    tok    = sumTxOk;
    tfail  = sumTxFail;
    r280   = sumRx280;
    r390   = sumRx390;
    r921   = sumRx921;
    r1016  = sumRx1016;
    portEXIT_CRITICAL(&stateMux);
    bool gate = parked || summon;
    twai_status_info_t st; twai_get_status_info(&st);
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
    s += ",\"rxMux1\":"  + String(rmx);
    s += ",\"txOk\":"    + String(tok);
    s += ",\"txFail\":"  + String(tfail);
    s += ",\"rx280\":"   + String(r280);
    s += ",\"rx390\":"   + String(r390);
    s += ",\"rx921\":"   + String(r921);
    s += ",\"rx1016\":"  + String(r1016);
    s += ",\"canState\":" + String((int)st.state);
    s += ",\"uptimeS\":"  + String((millis() - bootTime) / 1000);
    s += "}";
    return s;
}

static String blinkAStatsToJson() {
  bool en, ap, fmode;
  uint8_t req, man, curTurn;
  uint32_t tok, tfail;
  uint32_t now = millis();
  uint32_t dly, remain;
  uint8_t  pend;
  bool     armed;
  portENTER_CRITICAL(&blinkAMux);
  en      = blinkAEnabled;
  req     = blinkerReqA;
  man     = manualReq;
  curTurn = activeTurn;
  tok     = blkATxOk;
  tfail   = blkATxFail;
  dly     = blinkADelayMs;
  armed   = autoArmed;
  pend    = autoPendingDir;
  remain  = (autoArmed && (int32_t)(autoFireAt - now) > 0) ? (autoFireAt - now) : 0;
  portEXIT_CRITICAL(&blinkAMux);
  portENTER_CRITICAL(&stateMux);
  ap    = gateAPActive;
  fmode = forceMode;
  portEXIT_CRITICAL(&stateMux);
  uint32_t r249;
  uint8_t  rCnt, rTurn, rCk;
  bool     stOk, seen;
  portENTER_CRITICAL(&blinkAMux);
  r249  = rx249;   rCnt = realCounter; rTurn = realTurn; rCk = realCksum;
  stOk  = cksumSelfTest; seen = seen249;
  portEXIT_CRITICAL(&blinkAMux);
  String s = "{";
  s += "\"enabled\":"      + String(en ? "true" : "false");
  s += ",\"apActive\":"     + String(ap ? "true" : "false");
  s += ",\"forceMode\":"    + String(fmode ? "true" : "false");
  s += ",\"request\":"      + String(req);
  s += ",\"manual\":"       + String(man);
  s += ",\"activeTurn\":"   + String(curTurn);
  s += ",\"delayMs\":"      + String(dly);
  s += ",\"autoArmed\":"    + String(armed ? "true" : "false");
  s += ",\"autoPending\":"  + String(pend);
  s += ",\"autoRemainMs\":" + String(remain);
  s += ",\"txOk\":"         + String(tok);
  s += ",\"txFail\":"       + String(tfail);
  s += ",\"rx249\":"        + String(r249);
  s += ",\"seen249\":"      + String(seen ? "true" : "false");
  s += ",\"realCounter\":"  + String(rCnt);
  s += ",\"realTurn\":"     + String(rTurn);
  s += ",\"realCksum\":"    + String(rCk);
  s += ",\"cksumSelfTest\":"+ String(stOk ? "true" : "false");
  s += ",\"canAState\":"    + String((int)mcpState);
  s += ",\"uptimeS\":"      + String((millis() - bootTime) / 1000);
  s += "}";
  return s;
}

static String dasTelemetryStatsToJson() {
  uint32_t now = millis();
  String s = "{";
  s += "\"behaviorType\":" + String((int)visualBehaviorType);
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
  s += ",\"otaInProgress\":" + String(otaInProgress ? "true" : "false");
  s += ",\"otaSuccess\":"    + String(otaSuccess    ? "true" : "false");
  s += ",\"otaError\":"      + String(otaError      ? "true" : "false");
  s += ",\"otaErrMsg\":\""   + String(otaErrMsg) + "\"";
  s += ",\"otaBytes\":"      + String(otaBytes);
  s += ",\"otaTotal\":"      + String(otaTotal);
  s += "}";
  return s;
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
static void httpDasTelemetryStats() { server.send(200, "application/json", dasTelemetryStatsToJson()); }

static void httpRoot()   { server.send_P(200, "text/html", INDEX_HTML); }
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
    evaluateAutoBlinkerA();
    server.send(200, "application/json", summonStatsToJson());
}

static void httpBlinkAStats()  { server.send(200, "application/json", blinkAStatsToJson()); }
static void httpBlinkAEnable() {
    portENTER_CRITICAL(&blinkAMux); blinkAEnabled = true;  portEXIT_CRITICAL(&blinkAMux);
    evaluateAutoBlinkerA();
    summonCfgSave();
    server.send(200, "application/json", blinkAStatsToJson());
}
static void httpBlinkADisable() {
    portENTER_CRITICAL(&blinkAMux); blinkAEnabled = false; portEXIT_CRITICAL(&blinkAMux);
    evaluateAutoBlinkerA();
    summonCfgSave();
    server.send(200, "application/json", blinkAStatsToJson());
}
// Manual blinker trigger: ?dir=left|right|off
// Each press arms one soft one-shot pulse independently of the auto-blinker.
// "off" only cancels the active pulse and returns control to auto mode.
static void httpBlinkAManual() {
    uint8_t man = 0;
    if (server.hasArg("dir")) {
        String d = server.arg("dir");
        if      (d == "left")  man = 1;
        else if (d == "right") man = 2;
        else                   man = 0; // off / stop
    }
    portENTER_CRITICAL(&blinkAMux);
    manualReq = man;   // Dashboard display only; the pulse is handled below.
    if (man != 0) {
        oneShotTurn  = dirToTurn(man);       // 1=left->DOWN_1, 2=right->UP_1
        oneShotUntil = millis() + BLINKA_PULSE_MS;
        lastReqDir   = 0;  // ne bloque pas un futur front auto
    } else {
        oneShotTurn  = STALK_IDLE;
        oneShotUntil = 0;                    // stop immediat
    }
    portEXIT_CRITICAL(&blinkAMux);
    server.send(200, "application/json", blinkAStatsToJson());
}
// Set the auto-blinker delay in milliseconds (0..30000).
static void httpBlinkADelay() {
    if (server.hasArg("ms")) {
        long v = server.arg("ms").toInt();
        if (v < 0)     v = 0;
        if (v > 30000) v = 30000;
        portENTER_CRITICAL(&blinkAMux); blinkADelayMs = (uint32_t)v; portEXIT_CRITICAL(&blinkAMux);
        summonCfgSave();
    }
    server.send(200, "application/json", blinkAStatsToJson());
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
  Serial.printf("AP: SSID=%s IP=%s\n", ssid, ip.toString().c_str());

  server.on("/",                  HTTP_GET,  httpRoot);
  server.on("/api/summon/stats",  HTTP_GET,  httpSummonStats);
  server.on("/api/summon/enable", HTTP_POST, httpSummonEnable);
  server.on("/api/summon/disable",HTTP_POST, httpSummonDisable);
  server.on("/api/summon/tlssc-enable",  HTTP_POST, httpSummonTlsscEnable);
  server.on("/api/summon/tlssc-disable", HTTP_POST, httpSummonTlsscDisable);
  server.on("/api/summon/forcemode", HTTP_POST, httpSummonForceMode);
  server.on("/api/blinkA/stats",   HTTP_GET,  httpBlinkAStats);
  server.on("/api/blinkA/enable",  HTTP_POST, httpBlinkAEnable);
  server.on("/api/blinkA/disable", HTTP_POST, httpBlinkADisable);
  server.on("/api/blinkA/manual",  HTTP_POST, httpBlinkAManual);
  server.on("/api/blinkA/delay",   HTTP_POST, httpBlinkADelay);
  server.on("/api/system/stats",  HTTP_GET,  httpSystemStats);
  server.on("/api/das/stats",     HTTP_GET,  httpDasTelemetryStats);
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

// Properly reinitialize the MCP2515 (reset + bitrate + normal mode).
// Use the same MCP_CLOCK constant everywhere.
static void mcpReinit() {
  Can_A.reset();
  delay(10);                              // >=10 ms after reset (datasheet)
  Can_A.setBitrate(CAN_500KBPS, MCP_CLOCK);
  Can_A.setNormalMode();
  mcpTxFailConsecutive = 0;
}

static void canTaskMcp(void* arg) {
  Serial.println("[CAN A] MCP2515 task started");
  for (;;) {
    // ── Bounded read ──
    // Never drain more than MCP_RX_BUDGET frames before yielding the
    // task. If an RX buffer gets stuck ( uncleared overflow -> same
    // frame returned repeatedly), the task still exits the loop:
    // no infinite loop and no watchdog freeze.
    struct can_frame rxf;
    uint8_t budget = MCP_RX_BUDGET;
    while (budget-- && Can_A.readMessage(&rxf) == MCP2515::ERROR_OK) {
      mcpRxCount++;
      eapProcessMcpFrame(rxf);
    }

    // ── 0x249 transmission only during injection (~50 Hz) ──
    blinkATxTick();

    // ── State verification / recovery (1 Hz) ──
    unsigned long now = millis();
    if (now - lastMcpStatusMs >= 1000) {
      lastMcpStatusMs = now;

      // Read the real MCP2515 error flags (EFLG register).
      uint8_t eflg = Can_A.getErrorFlags();

      // 1) RX overflow: it MUST be cleared or the controller stops
      //    receiving in that buffer and reception appears frozen.
      if (eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) {
        Can_A.clearRXnOVR();
        Serial.println("[CAN A] RX overflow flags cleared");
      }

      // 2) Real bus-off via EFLG_TXBO, not just TX failures.
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
    twai_message_t f;
    while (twai_receive(&f, pdMS_TO_TICKS(2)) == ESP_OK) {
      canAnyFrames++;
      canRxBeat++;
      lastCanFrameMs = millis();

      switch (f.identifier) {
        case VISUAL_DEBUG_ID:
          if (f.data_length_code >= 8) {
            visualBehaviorType = (uint8_t)readBitsLE(f.data, 56, 2);
            visualDebugRxCount++;
            visualDebugLastMs = millis();
            evaluateAutoBlinkerA();
          }
          break;
        case 280:
          if (f.data_length_code >= 7) handle280(f.data);
          break;
        case 390:
          if (f.data_length_code >= 8) handle390(f.data);
          break;
        case 921:
          if (f.data_length_code >= 1) handle921(f.data);
          break;
        case 1016:
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

    // TWAI status check
    unsigned long now = millis();
    if (now - lastTwaiStatusMs >= 5000) {
      lastTwaiStatusMs = now;
      twai_status_info_t st;
      if (twai_get_status_info(&st) == ESP_OK) {
        if (st.state == TWAI_STATE_BUS_OFF) {
          Serial.println("[CAN B] TWAI bus-off, recovering...");
          twai_initiate_recovery();
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
// SETUP / LOOP
// ═══════════════════════════════════════════════════════════════

void setup() {
  bootTime = millis();
  Serial.begin(115200);
  delay(1500);

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
  summonCfgLoad();

  Serial.printf("Advanced EAP: TX 0x249 (inject) + RX 0x249/0x2E8 on CAN A (MCP2515)\n");
  Serial.printf("Summon enabled=%s\n", summonEnabled ? "true" : "false");
  Serial.printf("TLSSC enabled=%s (0x3FD mux0 bit38)\n", tlsscEnabled ? "true" : "false");
  Serial.printf("Auto-blinker: one-shot %d ms pulse, auto delay %u ms, soft UP_1/DOWN_1, counter aligned on real SCCM 0x249 (CAN A)\n", BLINKA_PULSE_MS, (unsigned)blinkADelayMs);

  // Start web task first (Core 0)
  BaseType_t retWeb = xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);
  if (retWeb != pdPASS) {
    Serial.printf("Web task creation failed: %d\n", retWeb);
    delay(3000);
    ESP.restart();
  }

  // Driver-wake delay before CAN init
  Serial.println("Driver-wake power detected. Waiting 10 seconds before CAN init...");
  delay(DRIVER_WAKE_DELAY_MS);

  // ══ Init CAN A (MCP2515) ══
  Serial.println("[CAN A] Initializing MCP2515...");
  pinMode(MCP2515_RST, OUTPUT);
  digitalWrite(MCP2515_RST, HIGH);
  delay(100);
  digitalWrite(MCP2515_RST, LOW);
  delay(100);
  digitalWrite(MCP2515_RST, HIGH);
  delay(100);

  SPI.begin(MCP2515_SCLK, MCP2515_MISO, MCP2515_MOSI, MCP2515_CS);

  Can_A.reset();
  delay(10);                              // >=10 ms after reset (datasheet)
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
  g.tx_queue_len = 16;
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
  delay(100);

  // Start CAN tasks
  BaseType_t retMcp = xTaskCreatePinnedToCore(canTaskMcp, "canA", 8192, nullptr, 5, nullptr, 1);
  if (retMcp != pdPASS) {
    Serial.printf("CAN A task creation failed: %d\n", retMcp);
    delay(3000);
    ESP.restart();
  }

  BaseType_t retTwai = xTaskCreatePinnedToCore(canTaskTwai, "canB", 8192, nullptr, 4, nullptr, 1);
  if (retTwai != pdPASS) {
    Serial.printf("CAN B task creation failed: %d\n", retTwai);
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
