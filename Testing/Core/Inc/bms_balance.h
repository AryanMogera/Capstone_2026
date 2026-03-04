//bms_balance.h

#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  PB_IDLE = 0,
  PB_BLEED_ON,
  PB_REST,
} pb_state_t;

typedef struct {
  pb_state_t st;
  uint32_t t_mark_ms;
  uint8_t cell_on;   // 0..3, 0xFF = none
} pb_ctx_t;

void PB_Init(pb_ctx_t *p);
void PB_Stop(pb_ctx_t *p);
void PB_Tick(pb_ctx_t *p, const float Vg[4], float Vmin, float dV,
             uint32_t now_ms, bool enable);
