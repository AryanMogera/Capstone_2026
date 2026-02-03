// bms_fsm.c
#include "bms_fsm.h"
#include "bms_config.h"
#include "bms_hw.h"
#include "bms_balance.h"
#include <math.h>

// balancing sub-fsm context
static bal_ctx_t g_bal;

	/* This is our Emergency Stop button. It communicates with our hardware layer (bms_hw.h)
 	 to physically disconnect the battery from both the charger and the load.
 	 It also shuts down the balancing resistors to prevent heat buildup.*/

static void all_off(void) {
  BMS_HW_SetChargeEnable(false);
  BMS_HW_SetDischargeEnable(false);
  BMS_HW_Balance_AllOff();
}

/*This function is the Safety Watch of our BMS. It continuously monitors
the battery's health by comparing live sensor data against predefined limits.
If any limit is crossed, it takes immediate action to protect the battery from damage or fire.*/

static void check_faults(bms_ctx_t *c) {
  // Over Voltage and Under Voltage
  if (c->Vmax > OV_LIMIT_V) c->faults |= FAULT_OV;
  if (c->Vmin < UV_LIMIT_V) c->faults |= FAULT_UV;

  // Over Temperature
  for (int i=0;i<8;i++) if (c->Tg[i] > OT_LIMIT_C) c->faults |= FAULT_OT;

  // Over Current (absolute)
  if (fabsf(c->I) > OC_LIMIT_A) c->faults |= FAULT_OC;

  if (c->faults) c->state = BMS_FAULT;		//If any of the above conditions were met, c->faults will no longer be zero.
}
/* 	Crucial Logic: Once the state is changed to BMS_FAULT,
    other parts of code (like all_off()) will see this and physically disconnect the battery MOSFETs.
  	This is a "latched" state, meaning even if the temperature cools down or the voltage stabilizes,
 	the battery stays off until a human or a reset command clears the fault.
 */


/*This function acts as the "Safety " for the balancing process.  Even if your cells are unbalanced,
 the BMS won't start bleeding off energy unless every single condition in this list is "Safe."
 It returns true only if it is both necessary and safe to balance.*/

static bool balance_allowed(const bms_ctx_t *c) {
  if (c->faults) return false;						//We dont want to engage balancing circuits while the main system is in fault
  if (!c->cmd_balance_enable) return false;			//Checks if we have actually turned on the balancing feature
  if (c->dV < DV_START_V) return false;				//dV is the difference between the highest and lowest cell, it only works when the gap exceeds DV_START_V
  if (fabsf(c->I) > BAL_MAX_CURR_A) return false;	//BMS waits for the battery current to be low and stable before it tursts the voltage readings enough to start balancing
  for (int i=0;i<8;i++) if (c->Tg[i] > BAL_MAX_TEMP_C) return false;	//if the battery is running hot, the BMS will refuse to balance to avoid adding heat to the pack
  return true;				//if all these conditions are skipped, it finally returns true and givies the balancing FSM green light to start
}

/*This function is the "Decision Maker" for the system.
  Its job is to determine which operational state the BMS should enter
  Its after it has finished taking its measurements.*/

static bms_state_t mode_select(const bms_ctx_t *c) {

	// You can later detect charge/discharge from current sign etc.Currently,
	//this function is set up for manual override. Instead of looking at sensors to guess what is happening,
	//it looks for a direct command from you (the user) or a higher-level controller.

  if (c->cmd_force_mode == 1) return BMS_CHARGE;
  if (c->cmd_force_mode == 2) return BMS_DISCHARGE;
  if (c->cmd_force_mode == 3) return BMS_DEEP_DISCHARGE;
  return BMS_DISCHARGE; // default "normal"
}

/*This is our Reset or Cold Boot function. Every time our microcontroller starts up,
  this code runs once to ensure the software starts from a clean, safe, and known state.*/

void BMS_Init(bms_ctx_t *c) {
  c->state = BMS_INIT;					//This places FSM at very first step
  c->faults = 0;						//This clears all safety flags
  for (int i=0;i<4;i++) c->Vg[i]=0;		//Sets the cell voltages to 0
  for (int i=0;i<8;i++) c->Tg[i]=25;	//Sets ther temperatures to 25 Celsius as a safe placeholder
  c->I=0;								//Sets the current to 0
  c->Vmax=0; c->Vmin=0; c->dV=0;		//Resets the calculates statistics

  c->cmd_balance_enable = 0;			//Balancing is OFF by default
  c->cmd_deep_enable = 0;				//Deep Discharge is OFF by default
  c->cmd_force_mode = 0;				//The mode is auto/standby rather than forced to Charging and Discharging

  BAL_Init(&g_bal);						//THis calls the initialization for the balancing logic itself
}

/*This function is the Data Processor. It takes the raw numbers provided by our sensors and
 calculates the statistics that the rest of the FSM uses to make decisions.*/

void BMS_UpdateDerived(bms_ctx_t *c) {		//TO find the extremes Vmax and Vmin
  c->Vmax = c->Vg[0];
  c->Vmin = c->Vg[0];
  for (int i=1;i<4;i++) {
    if (c->Vg[i] > c->Vmax) c->Vmax = c->Vg[i];
    if (c->Vg[i] < c->Vmin) c->Vmin = c->Vg[i];
  }
  c->dV = c->Vmax - c->Vmin;
}



void BMS_Step(bms_ctx_t *c, uint32_t now_ms) {
  switch (c->state) {
    case BMS_INIT:
      all_off();
      BAL_Stop(&g_bal);
      c->state = BMS_STANDBY;
      break;

    case BMS_STANDBY:
      // low activity waiting; you can add a wake condition
      all_off();
      c->state = BMS_MEASURE;
      break;

    case BMS_MEASURE:
      BMS_UpdateDerived(c);
      check_faults(c);
      if (c->state == BMS_FAULT) break;

      // After measure, choose mode
      c->state = mode_select(c);
      break;

    case BMS_CHARGE:
      // "enforce charge current limit - stop/limit at OV/OT/OC"
      BMS_HW_SetChargeEnable(true);
      BMS_HW_SetDischargeEnable(false);

      check_faults(c);
      if (c->state == BMS_FAULT) break;

      if (balance_allowed(c)) c->state = BMS_BALANCE;
      else c->state = BMS_MEASURE;
      break;

    case BMS_DISCHARGE:
      // "enforce discharge current limit - normal UV cutoff"
      BMS_HW_SetChargeEnable(false);
      BMS_HW_SetDischargeEnable(true);

      check_faults(c);
      if (c->state == BMS_FAULT) break;

      // optional deep-discharge path
      if (c->cmd_deep_enable && (c->Vmin < UV_LIMIT_V)) c->state = BMS_DEEP_DISCHARGE;
      else if (balance_allowed(c)) c->state = BMS_BALANCE;
      else c->state = BMS_MEASURE;
      break;

    case BMS_DEEP_DISCHARGE:
      // "controlled discharge toward 0V - hard safety stop"
      if (!c->cmd_deep_enable) { c->state = BMS_MEASURE; break; }

      BMS_HW_SetChargeEnable(false);
      BMS_HW_SetDischargeEnable(true);

      // hard stop (safety)
      if (c->Vmin < DEEP_UV_LIMIT_V) {
        c->faults |= FAULT_UV;
        c->state = BMS_FAULT;
        break;
      }

      check_faults(c);
      if (c->state == BMS_FAULT) break;

      c->state = BMS_MEASURE;
      break;

    case BMS_BALANCE: {
      // balancing sub-FSM
      bool en = balance_allowed(c);
      BAL_Tick(&g_bal, now_ms, en);

      // stop when dV small
      if (c->dV <= DV_STOP_V) {
        BAL_Stop(&g_bal);
        c->state = BMS_MEASURE;
      } else {
        c->state = BMS_MEASURE;
      }
    } break;

    case BMS_FAULT:
      // latch + log; all off
      all_off();
      BAL_Stop(&g_bal);
      // stays latched until reset command (you can add one)
      break;
  }
}
