#include "stm32f4xx.h"
#include "timebase.h"
#include "adc_dma.h"
#include "pwm_tim2.h"
#include "bms_fsm.h"
#include "bms_config.h"
#include <stdio.h>
#include "uart_vcp.h"

static void clock_init_minimal(void) {
  // Keep whatever CubeIDE startup gives you.
  // If you want a full RCC config, tell me your target SYSCLK.
}

// Placeholder conversions (replace with real divider/therm/hall equations)
static float group_v_from_adc(float vadc) { return vadc * 2.0f; } // TODO divider ratio
static float temp_c_from_adc(float vadc)  { (void)vadc; return 25.0f; } // TODO therm
static float curr_a_from_adc(float vadc)  { return (vadc - 1.65f) * 20.0f; } // TODO hall

static void send_telemetry(const bms_ctx_t *c) {
  char line[256];
  int k = snprintf(line, sizeof(line),
    "S=%u,F=0x%08lX,V=%.3f,%.3f,%.3f,%.3f,dV=%.3f,I=%.2f\r\n",
    (unsigned)c->state, (unsigned long)c->faults,
    (double)c->Vg[0],(double)c->Vg[1],(double)c->Vg[2],(double)c->Vg[3],
    (double)c->dV, (double)c->I
  );
  if (k > 0) UART2_WriteStr(line);
}

static void handle_cmd(char *line, bms_ctx_t *c) {
  // Commands:
  // BAL 0/1
  // DEEP 0/1
  // MODE AUTO/CHG/DSG/DEEP
  if (line[0]=='B' && line[1]=='A' && line[2]=='L') {
    int v = (line[4] == '1');
    c->cmd_balance_enable = (uint8_t)v;
    return;
  }
  if (line[0]=='D' && line[1]=='E' && line[2]=='E' && line[3]=='P') {
    int v = (line[5] == '1');
    c->cmd_deep_enable = (uint8_t)v;
    return;
  }
  if (line[0]=='M' && line[1]=='O' && line[2]=='D' && line[3]=='E') {
    if (line[5]=='A') c->cmd_force_mode = 0;
    else if (line[5]=='C') c->cmd_force_mode = 1;
    else if (line[5]=='D' && line[6]=='S') c->cmd_force_mode = 2;
    else if (line[5]=='D' && line[6]=='E') c->cmd_force_mode = 3;
    return;
  }
  if (line[0]=='R' && line[1]=='E' && line[2]=='S' && line[3]=='E' && line[4]=='T') {
    // reset faults (simple)
    c->faults = 0;
    c->state = BMS_INIT;
    return;
  }
}

int main(void) {
  clock_init_minimal();

  // If you know your clocks:
  // - cpu_hz for SysTick
  // - pclk1_hz for UART2
  // - tim2_clk for PWM_TIM2_Init
  // For bring-up, you can keep defaults; update later once RCC is finalized.
  timebase_init_1ms(16000000U);         // update if SYSCLK differs
  UART2_Init(16000000U, 115200);        // update with real PCLK1
  ADC_DMA_Init();
  ADC_DMA_Start();

  PWM_TIM2_Init(16000000U, 1000U);      // update with real TIM2 clock
  PWM_TIM2_AllOff();

  bms_ctx_t ctx;
  BMS_Init(&ctx);

  uint32_t t_meas = 0, t_tel = 0;
  char cmd[128];

  while (1) {
    uint32_t now = millis();

    // Commands from Python
    while (UART2_ReadLine(cmd, sizeof(cmd))) {
      handle_cmd(cmd, &ctx);
    }

    // Measurements (every MEASURE_PERIOD_MS)
    if (now - t_meas >= MEASURE_PERIOD_MS) {
      t_meas = now;

      const adc_frame_t *f = ADC_GetFrame();

      // Buffer order must match adc_dma.c:
      // [0..3]=V, [4..11]=T, [12]=I
      for (int i=0;i<4;i++) {
        float vadc = adc_raw_to_v(f->raw[i]);
        ctx.Vg[i] = group_v_from_adc(vadc);
      }

      for (int i=0;i<8;i++) {
        float vadc = adc_raw_to_v(f->raw[4+i]);
        ctx.Tg[i] = temp_c_from_adc(vadc);
      }

      float iadc = adc_raw_to_v(f->raw[12]);
      ctx.I = curr_a_from_adc(iadc);

      // Step the FSM
      BMS_Step(&ctx, now);
    }

    // Telemetry
    if (now - t_tel >= TELEMETRY_MS) {
      t_tel = now;
      send_telemetry(&ctx);
    }
  }
}

void USART2_IRQHandler(void) { UART2_OnRxIRQ(); }
