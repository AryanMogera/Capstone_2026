// bms_fsm.c
#include "bms_fsm.h"
#include "bms_config.h"
#include "bms_hw.h"
#include "bms_balance.h"
#include <math.h>

static pb_ctx_t g_pb;

static void all_off(void) {
  BMS_HW_SetChargeEnable(false);
  BMS_HW_SetDischargeEnable(false);
  BMS_HW_Bleed_AllOff();
}

static void check_faults(bms_ctx_t *c) {
  // In deep discharge, use the deep UV threshold; otherwise use normal UV.
  float uv = (c->state == BMS_DEEP_DISCHARGE) ? DEEP_UV_LIMIT_V : UV_LIMIT_V;

  if (c->Vmax > OV_LIMIT_V) c->faults |= FAULT_OV;
  if (c->Vmin < uv)         c->faults |= FAULT_UV;

  for (int i = 0; i < 8; i++) {
    if (c->Tg[i] > OT_LIMIT_C) c->faults |= FAULT_OT;
  }

  if (fabsf(c->I) > OC_LIMIT_A) c->faults |= FAULT_OC;

  if (c->faults) c->state = BMS_FAULT;
}

// Fully automatic balancing (no UART enable flag)
static bool balance_allowed(const bms_ctx_t *c) {
  if (c->faults) return false;

  // Charge-only balancing
  if (!c->charger_connected) return false;

  if (c->dV < DV_START_V) return false;
  if (c->dV <= DV_STOP_V) return false; // extra guard
  if (fabsf(c->I) > BAL_MAX_CURR_A) return false;

  for (int i = 0; i < 8; i++) {
    if (c->Tg[i] > BAL_MAX_TEMP_C) return false;
  }

  return true;
}

static bms_state_t mode_select(const bms_ctx_t *c) {
  // Deep discharge toggled by 5s USER button (latched deep_mode).
  // Safety: do not allow deep discharge while charger is connected.
  if (c->deep_mode && !c->charger_connected) return BMS_DEEP_DISCHARGE;

  // Auto mode: charger connected -> CHARGE, else DISCHARGE
  if (c->charger_connected) return BMS_CHARGE;
  return BMS_DISCHARGE;
}

void BMS_Init(bms_ctx_t *c) {
  c->state = BMS_INIT;
  c->faults = 0;

  for (int i = 0; i < 4; i++) c->Vg[i] = 0.0f;
  for (int i = 0; i < 8; i++) c->Tg[i] = 25.0f;
  c->I = 0.0f;

  c->Vmax = 0.0f;
  c->Vmin = 0.0f;
  c->dV   = 0.0f;

  c->charger_connected = false;  // set by main via ADC charger detect
  c->deep_mode         = false;  // toggled by 5s USER button in main

  PB_Init(&g_pb);
}

void BMS_UpdateDerived(bms_ctx_t *c) {
  c->Vmax = c->Vg[0];
  c->Vmin = c->Vg[0];

  for (int i = 1; i < 4; i++) {
    if (c->Vg[i] > c->Vmax) c->Vmax = c->Vg[i];
    if (c->Vg[i] < c->Vmin) c->Vmin = c->Vg[i];
  }

  c->dV = c->Vmax - c->Vmin;
}

void BMS_Step(bms_ctx_t *c, uint32_t now_ms) {
  switch (c->state) {

    case BMS_INIT:
      all_off();
      PB_Stop(&g_pb);
      c->state = BMS_STANDBY;
      break;

    case BMS_STANDBY:
      all_off();
      c->state = BMS_MEASURE;
      break;

    case BMS_MEASURE:
      BMS_UpdateDerived(c);
      check_faults(c);
      if (c->state == BMS_FAULT) break;

      c->state = mode_select(c);
      break;

    case BMS_CHARGE:
      // Ensure bleed only happens in BMS_BALANCE
      BMS_HW_Bleed_AllOff();

      BMS_HW_SetChargeEnable(true);
      BMS_HW_SetDischargeEnable(false);

      BMS_UpdateDerived(c);
      check_faults(c);
      if (c->state == BMS_FAULT) break;

      if (balance_allowed(c)) c->state = BMS_BALANCE;
      else c->state = BMS_MEASURE;
      break;

    case BMS_DISCHARGE:
      // Ensure bleed only happens in BMS_BALANCE
      BMS_HW_Bleed_AllOff();

      BMS_HW_SetChargeEnable(false);
      BMS_HW_SetDischargeEnable(true);

      BMS_UpdateDerived(c);
      check_faults(c);
      if (c->state == BMS_FAULT) break;

      c->state = BMS_MEASURE;
      break;

    case BMS_DEEP_DISCHARGE:
      // Ensure bleed only happens in BMS_BALANCE
      BMS_HW_Bleed_AllOff();

      // Exit deep mode if toggled off or if charger gets connected (safety)
      if (!c->deep_mode || c->charger_connected) {
        all_off();
        c->state = BMS_MEASURE;
        break;
      }

      BMS_HW_SetChargeEnable(false);
      BMS_HW_SetDischargeEnable(true);

      BMS_UpdateDerived(c);
      check_faults(c);
      if (c->state == BMS_FAULT) break;

      // stay in deep while deep_mode remains true
      break;

    case BMS_BALANCE: {
      // In balance state, charge should still be enabled (charge-only balancing)
      BMS_HW_SetChargeEnable(true);
      BMS_HW_SetDischargeEnable(false);

      BMS_UpdateDerived(c);
      check_faults(c);
      if (c->state == BMS_FAULT) { PB_Stop(&g_pb); break; }

      bool en = balance_allowed(c);

      // Passive balancing tick (bleeds highest group in time slices)
      PB_Tick(&g_pb, c->Vg, c->Vmin, c->dV, now_ms, en);

      // After a balance tick, re-measure / re-decide
      c->state = BMS_MEASURE;
    } break;

    case BMS_FAULT:
      all_off();
      PB_Stop(&g_pb);
      // latched until reset (your main can force c->state=BMS_INIT and clear faults)
      break;

    default:
      c->faults |= FAULT_SENSOR;
      c->state = BMS_FAULT;
      break;
  }
}
