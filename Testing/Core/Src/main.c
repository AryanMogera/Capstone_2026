// main.c
#include "stm32f4xx.h"
#include "timebase.h"
#include "adc_dma.h"
#include "bms_fsm.h"
#include "bms_config.h"
#include "bms_hw.h"
#include <stdio.h>
#include "uart_vcp.h"

static void clock_init_minimal(void) {}

// Placeholder conversions (replace with real divider/therm/hall equations)
static float group_v_from_adc(float vadc) { return vadc * 2.0f; }            // TODO divider ratio
static float temp_c_from_adc(float vadc)  { (void)vadc; return 25.0f; }      // TODO therm
static float curr_a_from_adc(float vadc)  { return (vadc - 1.65f) * 20.0f; } // TODO hall

// Charger detect
static bool charger_present_update(bool prev, float vdet) {
  if (prev) return (vdet >= CHG_DET_OFF_V);
  else      return (vdet >= CHG_DET_ON_V);
}



#define DEEP_LONGPRESS_MS 5000u
#define BTN_DEBOUNCE_MS     30u

static void deep_button_update(bms_ctx_t *c, uint32_t now_ms) {
  static bool last_raw = false;
  static bool stable = false;
  static uint32_t t_last_change = 0;

  static bool armed = true;          // re-arm after release
  static uint32_t t_press_start = 0; // 0 = not pressed

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
      c->deep_mode = !c->deep_mode;  // TOGGLE deep discharge mode
      armed = false;                 // prevent re-toggle while still held
    }
  } else {
    // released -> re-arm
    t_press_start = 0u;
    armed = true;
  }
}

/* telemetry */

static void send_telemetry(const bms_ctx_t *c) {
  char line[256];

  int k = snprintf(line, sizeof(line),
    "{\"v\":[%.3f,%.3f,%.3f,%.3f],\"i\":%.2f,\"chg\":%u,\"deep\":%u,\"fault\":%lu}\n",
    (double)c->Vg[0], (double)c->Vg[1], (double)c->Vg[2], (double)c->Vg[3],
    (double)c->I,
    (unsigned)(c->charger_connected ? 1u : 0u),
    (unsigned)(c->deep_mode ? 1u : 0u),
    (unsigned long)c->faults
  );

  if (k > 0) UART2_WriteStr(line);
}



static void handle_cmd(char *line, bms_ctx_t *c) {
  // Commands:
  // RESET
  if (line[0]=='R' && line[1]=='E' && line[2]=='S' && line[3]=='E' && line[4]=='T') {
    c->faults = 0;
    c->state = BMS_INIT;
    c->deep_mode = false; // optional: reset deep mode too
    return;
  }
}



int main(void) {
  clock_init_minimal();

  timebase_init_1ms(16000000U);   // update if SYSCLK differs
  UART2_Init(16000000U, 115200);  // update with real PCLK1

  ADC_DMA_Init();
  ADC_DMA_Start();

  BMS_HW_Init();

  bms_ctx_t ctx;
  BMS_Init(&ctx);

  uint32_t t_meas = 0, t_tel = 0;
  char cmd[128];

  while (1) {
    uint32_t now = millis();

   
    deep_button_update(&ctx, now);

    // Optional UART reset handling
    while (UART2_ReadLine(cmd, sizeof(cmd))) {
      handle_cmd(cmd, &ctx);
    }

    if (now - t_meas >= MEASURE_PERIOD_MS) {
      t_meas = now;

      const adc_frame_t *f = ADC_GetFrame();

      // V1..V4 = raw[0..3]
      for (int i=0;i<4;i++) {
        float vadc = adc_raw_to_v(f->raw[i]);
        ctx.Vg[i] = group_v_from_adc(vadc);
      }

      // T1..T8 = raw[4..11]
      for (int i=0;i<8;i++) {
        float vadc = adc_raw_to_v(f->raw[4+i]);
        ctx.Tg[i] = temp_c_from_adc(vadc);
      }

      // I1 = raw[12]
      float iadc = adc_raw_to_v(f->raw[12]);
      ctx.I = curr_a_from_adc(iadc);

      // Charger detect = raw[13]  (PA7 / ADC1_IN7)
      float chg_vadc = adc_raw_to_v(f->raw[13]);
      ctx.charger_connected = charger_present_update(ctx.charger_connected, chg_vadc);

      // FSM step (balancing is automatic inside FSM)
      BMS_Step(&ctx, now);
    }

    if (now - t_tel >= TELEMETRY_MS) {
      t_tel = now;
      send_telemetry(&ctx);
    }
  }
}

void USART2_IRQHandler(void) { UART2_OnRxIRQ(); }
