// bms_fsm.c

#include "bms_fsm.h"
#include "bms_config.h"
#include "bms_hw.h"
#include "bms_balance.h"
#include <math.h>

static pb_ctx_t g_pb;						 // static only this file can access it. pb is passive balancing logic

static void all_off(void) { 				// emergency safe function to reuse in INIT, STANDBY, FAULT, and deep-discharge
  BMS_HW_SetChargeEnable(false); 			//disable charge MOSFET and discharge MOSFET, and turns off all bleed resistors.
  BMS_HW_SetDischargeEnable(false);
  BMS_HW_Bleed_AllOff();
}

static void check_faults(bms_ctx_t *c) {

  float uv = (c->state == BMS_DEEP_DISCHARGE) ? DEEP_UV_LIMIT_V : UV_LIMIT_V;  //choose one

  if (c->Vmax > OV_LIMIT_V) c->faults |= FAULT_OV; 			//overvoltage fault limit
  if (c->Vmin < uv) c->faults |= FAULT_UV;       			//undervoltage limit

  for (int i = 0; i < 8; i++) {    							//loop through 8 temp and one current
    if (c->Tg[i] > OT_LIMIT_C) c->faults |= FAULT_OT;		// if any temp is high set overtemp fault
  }

  if (fabsf(c->I) > OC_LIMIT_A) c->faults |= FAULT_OC;     //absolute value of current, set OC fault
  if (c->faults) c->state = BMS_FAULT;					   //if there are any faults then force the fsm to BSM_FAULT
}

static bool balance_allowed(const bms_ctx_t *c) {           //checks if balance conditions are met
  if (c->faults) return false;								// no faults
  if (!c->cmd_balance_enable) return false;					//software comman to enable balance

  if (!c->charger_connected) return false;					// check charger is connected

  if (c->dV < DV_START_V) return false;     				//cell group voltage is toos small, no need to balance
  if (fabsf(c->I) > BAL_MAX_CURR_A) return false;			//check if current is too high

  for (int i = 0; i < 8; i++) {								// if temp is too high
    if (c->Tg[i] > BAL_MAX_TEMP_C) return false;
  }

  return true;  											// only reaches here if all checks passed
}

static bool deep_done(const bms_ctx_t *c) {					//check if deep discharge is done when the highest group voltage is below a near-zero threshold.

  return (c->Vmax <= DEEP_DONE_V);							// using VMAx ensures all groups are near zero, not just one
}

static bms_state_t mode_select(const bms_ctx_t *c) {		//returns which state the fsm should go to from MEASURE

  if (c->cmd_force_mode == 1) return BMS_CHARGE;			// manual override, 1 = force charge, 2 = force discharge
  if (c->cmd_force_mode == 2) return BMS_DISCHARGE;


  if (c->cmd_deep_enable && c->deep_latched) return BMS_DEEP_DISCHARGE;  //deep discharge only if , deep enabled by command and 5 second button hold latch has


  if (c->charger_connected) return BMS_CHARGE;  // charge mode if charger is connected
  return BMS_DISCHARGE; 						// or else discharge
}

void BMS_Init(bms_ctx_t *c) {					// start fsm in init and clear faults
  c->state = BMS_INIT;
  c->faults = 0;

  for (int i = 0; i < 4; i++) c->Vg[i] = 0.0f;		//initialize 4 group voltages
  for (int i = 0; i < 8; i++) c->Tg[i] = 25.0f;     //initialize tempt to a room temp value
  c->I = 0.0f;										//

  c->Vmax = 0.0f;									//derived values
  c->Vmin = 0.0f;
  c->dV   = 0.0f;

  c->cmd_balance_enable = 1;						//no balancing, deep discharge or forced mode
  c->cmd_deep_enable    = 1;
  c->cmd_force_mode     = 0;

  c->cmd_chg_force_en  = 0;
  c->cmd_chg_force_val = 0;

  c->charger_connected  = false;					//assume charger not connected, deep button not pressed
  c->deep_button        = false;


  c->deep_latched        = false;					//deep latch starts off false

  PB_Init(&g_pb);									// inintialized balancing module context
}

void BMS_UpdateDerived(bms_ctx_t *c) {				//start with the first group voltage as max and min
  c->Vmax = c->Vg[0];
  c->Vmin = c->Vg[0];

  for (int i = 1; i < 4; i++) {						// scan the remaining 3 group voltages and update max/min
    if (c->Vg[i] > c->Vmax) c->Vmax = c->Vg[i];
    if (c->Vg[i] < c->Vmin) c->Vmin = c->Vg[i];
  }

  c->dV = c->Vmax - c->Vmin;						// compute voltage spread used for balancing decisions
}

void BMS_Step(bms_ctx_t *c, uint32_t now_ms) { 		//fsm advances based on current  c->state and now_ms is current time in milliseconds
  switch (c->state) {

    case BMS_INIT:
      all_off();									//ensures everything is off
      PB_Stop(&g_pb);								// stop balancing logic
      c->state = BMS_STANDBY;						// move to standby next
      break;

    case BMS_STANDBY:
      all_off();									//still keep everything off
      c->state = BMS_MEASURE;						//next go measure and decide what to do
      break;

    case BMS_MEASURE:
      BMS_UpdateDerived(c);							//update max/min/spread
      check_faults(c);								//check safety faults; if fault then stop here
      if (c->state == BMS_FAULT) break;

      c->state = mode_select(c);					//otherwise choose the next mode
      break;

    case BMS_CHARGE:

      BMS_HW_Bleed_AllOff();						// bleed resistors only in balance state

      BMS_HW_SetChargeEnable(true);					//turn on charge path
      BMS_HW_SetDischargeEnable(false);				// turn off discharge path

      BMS_UpdateDerived(c);							//recompute derived values and recheck safety
      check_faults(c);
      if (c->state == BMS_FAULT) break;

      if (balance_allowed(c)) c->state = BMS_BALANCE; // go to balance if balancing is allowed
      else c->state = BMS_MEASURE; 						//otherwise measure
      break;

    case BMS_DISCHARGE:

      BMS_HW_Bleed_AllOff();						//again ensure bleed is off outside balance

      BMS_HW_SetChargeEnable(false);				//turn of charge
      BMS_HW_SetDischargeEnable(true);				//turn on discharge

      BMS_UpdateDerived(c);							//update plus check faults
      check_faults(c);
      if (c->state == BMS_FAULT) break;

      c->state = BMS_MEASURE;						// then go back to measure and decide again
      break;

    case BMS_DEEP_DISCHARGE:

      BMS_HW_Bleed_AllOff();						// bleed off unless in balance


      if (!(c->cmd_deep_enable && c->deep_latched)) { //if deep mode is disabled or latch is unset, deep discharge is cancelled
        all_off();										//turn off everything
        c->state = BMS_MEASURE;							//return to measure
        break;
      }


      BMS_HW_SetChargeEnable(false);  					//main discharge pathh on
      BMS_HW_SetDischargeEnable(true);


      BMS_UpdateDerived(c);								//keep Vmax,Vmin, dV updated


      if (c->I > DEEP_MAX_CURR_A) {						//deep mode specific overcurrent limit
        c->faults |= FAULT_OC;							//notice check only positive current discharge directions
        c->state  = BMS_FAULT;
        break;
      }


      if (deep_done(c)) {								// if near - zero reached then shut everything off
        all_off();
        c->deep_latched = false;						//clear deep latch so it wont restart without anaother 5 s hold
        c->state = BMS_STANDBY;							// go to standby
        break;
      }


      check_faults(c);									//fault check
      if (c->state == BMS_FAULT) break;


      break;											//stain deep until done  or cancel or fault

    case BMS_BALANCE: {

      BMS_HW_SetChargeEnable(true);						//balance is charge only so ti keeps charge path enabled.
      BMS_HW_SetDischargeEnable(false);

      BMS_UpdateDerived(c);								//update plus check faults
      check_faults(c);
      if (c->state == BMS_FAULT) { PB_Stop(&g_pb); break; } //break if faulted

      bool en = balance_allowed(c);							//recheck if balance is allowed right now
      PB_Tick(&g_pb, c->Vg, c->Vmin, c->dV, now_ms, en);    // run one balance tick


      c->state = BMS_MEASURE; 								//afteer balance tick go to measure
    } break;

    case BMS_FAULT:											// if faulted then turn of everything and stop balance
      all_off();
      PB_Stop(&g_pb);

      break;

    default:												//should never happen
      c->faults |= FAULT_SENSOR;							// invalid state values appears then move to fault
      c->state = BMS_FAULT;
      break;
  }
}
