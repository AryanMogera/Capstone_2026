// main.c
// - Voltage: resistor divider
// - Temp: Murata NXRT15XH103FA1B030 (10k @25C, Beta=3380K) using Beta equation
// - Current: generic offset + sensitivity (Hall/INA style)
// NOTE: MUST set the hardware constants below (divider resistors + current sensor params)

#include "stm32f4xx.h"
#include "timebase.h"
#include "adc_dma.h"
#include "bms_fsm.h"
#include "bms_config.h"
#include "bms_hw.h"
#include <stdio.h>
#include <math.h>
#include "uart_vcp.h"

static void clock_init_minimal(void) {}

/* -- ADC common -- */

#define VREF   3.3f
#define ADCMAX 4095.0f

static inline float adc_raw_to_v_local(uint16_t raw){
  return (raw * VREF) / ADCMAX;
}

/* -- 1) Group voltage from divider --
   Assuming: VBAT -> R_TOP -> ADC node -> R_BOT -> GND
   Vbat = Vadc * (R_TOP + R_BOT) / R_BOT
*/
#define VDIV_RTOP_OHMS  100000.0f   // TODO: set top resistor value
#define VDIV_RBOT_OHMS  10000.0f    // TODO: set bottom resistor value

static inline float group_v_from_adc(float vadc){
  return vadc * ((VDIV_RTOP_OHMS + VDIV_RBOT_OHMS) / VDIV_RBOT_OHMS);
}

/* -- 2) Thermistor temperature --
   Thermistor: Murata NXRT15XH103FA1B030
   - R0 = 10k @ 25C
   - Beta = 3380K (25/50)
   Divider assumption: 3.3V -> R_FIXED -> ADC node -> NTC -> GND
   Rntc = R_FIXED * V / (VREF - V)
   T(K) = 1 / (1/T0 + (1/B)*ln(R/R0))
*/
#define NTC_R0_OHMS     10000.0f
#define NTC_BETA_K      3380.0f
#define T0_K            298.15f     // 25C in Kelvin

#define TH_RFIXED_OHMS  39000.0f    // TODO: set your fixed resistor in therm divider 39kΩ

static inline float ntc_resistance_from_v(float v){
  // clamp to avoid division by zero
  if (v < 0.001f) v = 0.001f;
  if (v > (VREF - 0.001f)) v = VREF - 0.001f;

  // 3.3V -> R_FIXED -> node -> NTC -> GND
  return TH_RFIXED_OHMS * (v / (VREF - v));
}

static inline float temp_c_from_adc(float vadc){
  float r = ntc_resistance_from_v(vadc);
  float invT = (1.0f / T0_K) + (1.0f / NTC_BETA_K) * logf(r / NTC_R0_OHMS);
  float Tk = 1.0f / invT;
  return Tk - 273.15f;
}

/* If thermistor divider is wired as:
   3.3V -> NTC -> node -> R_FIXED -> GND
   then use this instead:
   return TH_RFIXED_OHMS * ((VREF - v) / v);
*/

/* -- 3) Current conversion --
   Generic: Vout = Vzero + I * Sens
   => I = (Vadc - Vzero) / Sens

   Set these from your sensor:
   - Vzero: measured ADC voltage at 0A (often ~1.65V)
   - Sens: volts per amp (e.g., 0.066 V/A)
*/
#define I_VZERO_V        1.65f      // TODO: measure at 0A and set
#define I_SENS_V_PER_A   0.066f     // TODO: set from your sensor or 2-point calibration

static inline float curr_a_from_adc(float vadc){
  return (vadc - I_VZERO_V) / I_SENS_V_PER_A;
}

/* -- Charger detect: hysteresis -- */

static bool charger_present_update(bool prev, float vdet) {
  if (prev) return (vdet >= CHG_DET_OFF_V);
  else      return (vdet >= CHG_DET_ON_V);
}

/* -- 5s USER-button long-press toggle for deep_mode -- */

#define DEEP_LONGPRESS_MS 5000u
#define BTN_DEBOUNCE_MS     30u

static void deep_button_update(bms_ctx_t *c, uint32_t now_ms) {
  static bool last_raw = false;
  static bool stable = false;
  static uint32_t t_last_change = 0;

  static bool armed = true;
  static uint32_t t_press_start = 0;

  bool raw = BMS_HW_UserButtonPressed(); // true while physically pressed

  // debounce
  if (raw != last_raw) {
    last_raw = raw;
    t_last_change = now_ms;
  }
  if ((now_ms - t_last_change) >= BTN_DEBOUNCE_MS) {
    stable = raw;
  }

  // long-press toggle
  if (stable) {
    if (t_press_start == 0u) t_press_start = now_ms;

    if (armed && (now_ms - t_press_start) >= DEEP_LONGPRESS_MS) {
      c->deep_mode = !c->deep_mode;
      armed = false;
    }
  } else {
    t_press_start = 0u;
    armed = true;
  }
}

/* -- telemetry -- */

static void send_telemetry(const bms_ctx_t *c) {
  char line[320];

  int k = snprintf(line, sizeof(line),
    "{\"s\":%u,"
    "\"v\":[%.3f,%.3f,%.3f,%.3f],"
    "\"tc\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f],"
    "\"i\":%.2f,"
    "\"chg\":%u,"
    "\"deep\":%u,"
    "\"fault\":%lu}\n",
    (unsigned)c->state,
    (double)c->Vg[0], (double)c->Vg[1], (double)c->Vg[2], (double)c->Vg[3],
    (double)c->Tg[0], (double)c->Tg[1], (double)c->Tg[2], (double)c->Tg[3],
    (double)c->Tg[4], (double)c->Tg[5], (double)c->Tg[6], (double)c->Tg[7],
    (double)c->I,
    (unsigned)(c->charger_connected ? 1u : 0u),
    (unsigned)(c->deep_mode ? 1u : 0u),
    (unsigned long)c->faults
  );

  if (k > 0) UART2_WriteStr(line);
}

/* -- UART commands: RESET only -- */

static void handle_cmd(char *line, bms_ctx_t *c) {
  if (line[0]=='R' && line[1]=='E' && line[2]=='S' && line[3]=='E' && line[4]=='T') {
    c->faults = 0;
    c->state = BMS_INIT;
    c->deep_mode = false;
    return;
  }
}

/* -- main -- */

int main(void) {
  clock_init_minimal();

  timebase_init_1ms(16000000U);
  UART2_Init(16000000U, 115200);

  ADC_DMA_Init();
  ADC_DMA_Start();

  BMS_HW_Init();

  bms_ctx_t ctx;
  BMS_Init(&ctx);

  uint32_t t_meas = 0, t_tel = 0;
  char cmd[128];

  while (1) {
    uint32_t now = millis();

    // 5-second long press toggle
    deep_button_update(&ctx, now);

    // Optional UART reset handling
    while (UART2_ReadLine(cmd, sizeof(cmd))) {
      handle_cmd(cmd, &ctx);
    }

    if (now - t_meas >= MEASURE_PERIOD_MS) {
      t_meas = now;

      const adc_frame_t *f = ADC_GetFrame();

      // V1..V4 = raw[0..3] -> group voltages
      for (int i=0;i<4;i++) {
        float vadc = adc_raw_to_v_local(f->raw[i]);
        ctx.Vg[i] = group_v_from_adc(vadc);
      }

      // T1..T8 = raw[4..11] -> temperatures
      for (int i=0;i<8;i++) {
        float vadc = adc_raw_to_v_local(f->raw[4+i]);
        ctx.Tg[i] = temp_c_from_adc(vadc);
      }

      // I1 = raw[12] -> current
      float iadc_v = adc_raw_to_v_local(f->raw[12]);
      ctx.I = curr_a_from_adc(iadc_v);

      // Charger detect = raw[13] (PA7 / ADC1_IN7)
      float chg_vadc = adc_raw_to_v_local(f->raw[13]);
      ctx.charger_connected = charger_present_update(ctx.charger_connected, chg_vadc);

      // FSM step
      BMS_Step(&ctx, now);
    }

    if (now - t_tel >= TELEMETRY_MS) {
      t_tel = now;
      send_telemetry(&ctx);
    }
  }
}

void USART2_IRQHandler(void) { UART2_OnRxIRQ(); }
