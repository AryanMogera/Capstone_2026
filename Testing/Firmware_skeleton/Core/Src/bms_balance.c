// bms_balance.c
#include "bms_balance.h"
#include "bms_hw.h"
#include "bms_config.h"
#include "timebase.h"

void BAL_Init(bal_ctx_t *b) {
  b->st = BAL_IDLE;
  b->t_mark_ms = 0;
}

void BAL_Stop(bal_ctx_t *b) {
  b->st = BAL_IDLE;
  BMS_HW_Balance_AllOff();
}

void BAL_Tick(bal_ctx_t *b, uint32_t now_ms, bool enable) {
  if (!enable) { BAL_Stop(b); return; }

  switch (b->st) {
    case BAL_IDLE:
      // pick high/low groups (later). For now just run S1/S2 waveform.
      b->st = BAL_CHARGE_CAP;
      b->t_mark_ms = now_ms;
      break;

    case BAL_CHARGE_CAP:
      // S1 ON (PWM), S2 OFF
      BMS_HW_Balance_S1(50);
      BMS_HW_Balance_S2(0);
      if ((now_ms - b->t_mark_ms) >= BAL_TCHARGE_MS) {
        b->st = BAL_DEADTIME;
        b->t_mark_ms = now_ms;
        BMS_HW_Balance_AllOff();
        delay_us(BAL_DEAD_US);
      }
      break;

    case BAL_DEADTIME:
      // both off already
      if ((now_ms - b->t_mark_ms) >= 1) { // small spacing
        b->st = BAL_DISCHARGE_CAP;
        b->t_mark_ms = now_ms;
      }
      break;

    case BAL_DISCHARGE_CAP:
      // S2 ON (PWM), S1 OFF
      BMS_HW_Balance_S1(0);
      BMS_HW_Balance_S2(50);
      if ((now_ms - b->t_mark_ms) >= BAL_TDISCH_MS) {
        b->st = BAL_IDLE;
        b->t_mark_ms = now_ms;
        BMS_HW_Balance_AllOff();
        delay_us(BAL_DEAD_US);
      }
      break;
  }
}
