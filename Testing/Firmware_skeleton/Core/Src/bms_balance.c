#include "bms_balance.h"
#include "bms_hw.h"
#include "bms_config.h"

#define PB_ON_MS   300u
#define PB_OFF_MS   80u

static uint8_t pick_high_cell(const float Vg[4]) {
  uint8_t imax = 0;
  float vmax = Vg[0];
  for (uint8_t i=1;i<4;i++){
    if (Vg[i] > vmax) { vmax = Vg[i]; imax = i; }
  }
  return imax;
}

void PB_Init(pb_ctx_t *p) {
  p->st = PB_IDLE;
  p->t_mark_ms = 0;
  p->cell_on = 0xFF;
}

void PB_Stop(pb_ctx_t *p) {
  p->st = PB_IDLE;
  p->cell_on = 0xFF;
  BMS_HW_Bleed_AllOff();
}

void PB_Tick(pb_ctx_t *p, const float Vg[4], float Vmin, float dV,
             uint32_t now_ms, bool enable) {
  if (!enable) { PB_Stop(p); return; }

  // if already balanced enough, stop
  if (dV <= DV_STOP_V) { PB_Stop(p); return; }

  switch (p->st) {
    case PB_IDLE: {
      // only start bleeding when we truly need it
      if (dV < DV_START_V) { BMS_HW_Bleed_AllOff(); return; }

      p->cell_on = pick_high_cell(Vg);


      if ((Vg[p->cell_on] - Vmin) < DV_START_V) {
        BMS_HW_Bleed_AllOff();
        return;
      }

      BMS_HW_Bleed_AllOff();
      BMS_HW_Bleed_Set(p->cell_on, true);
      p->t_mark_ms = now_ms;
      p->st = PB_BLEED_ON;
    } break;

    case PB_BLEED_ON:
      if ((now_ms - p->t_mark_ms) >= PB_ON_MS) {
        BMS_HW_Bleed_AllOff();
        p->t_mark_ms = now_ms;
        p->st = PB_REST;
      }
      break;

    case PB_REST:
      if ((now_ms - p->t_mark_ms) >= PB_OFF_MS) {
        p->st = PB_IDLE;
      }
      break;
  }
}
