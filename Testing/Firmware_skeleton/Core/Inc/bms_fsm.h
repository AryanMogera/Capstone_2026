//bms_fsm.h
#ifndef BMS_FSM_H_
#define BMS_FSM_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  BMS_INIT=0,
  BMS_STANDBY,
  BMS_MEASURE,
  BMS_CHARGE,
  BMS_DISCHARGE,
  BMS_DEEP_DISCHARGE,
  BMS_BALANCE,
  BMS_FAULT
} bms_state_t;

enum {
  FAULT_OV 		= (1U<<0),
  FAULT_UV 		= (1U<<1),
  FAULT_OT 		= (1U<<2),
  FAULT_OC 		= (1U<<3),
  FAULT_SENSOR 	= (1U<<4)
};

typedef struct {
  bms_state_t state;
  uint32_t faults;

  // processed measurements
  float Vg[4];
  float Tg[8];
  float I;

  float Vmax, Vmin, dV;

  // commands / mode select
  uint8_t cmd_balance_enable;
  uint8_t cmd_deep_enable;
  uint8_t cmd_force_mode; // 0=auto, 1=charge, 2=discharge, 3=deep
} bms_ctx_t;

void BMS_Init(bms_ctx_t *c);
void BMS_UpdateDerived(bms_ctx_t *c);
void BMS_Step(bms_ctx_t *c, uint32_t now_ms);


#endif /* BMS_FSM_H_ */
