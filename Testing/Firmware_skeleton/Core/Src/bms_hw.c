// bms_hw.c
#include "bms_hw.h"
#include "pwm_tim2.h"

void BMS_HW_Init(void) {
  BMS_HW_SetChargeEnable(false);
  BMS_HW_SetDischargeEnable(false);
  BMS_HW_Balance_AllOff();
}

void BMS_HW_SetChargeEnable(bool on) {
  (void)on; // TODO: map to GPIO later
}

void BMS_HW_SetDischargeEnable(bool on) {
  (void)on; // TODO: map to GPIO later
}

void BMS_HW_Balance_AllOff(void) { PWM_TIM2_AllOff(); }
void BMS_HW_Balance_S1(uint8_t duty) { PWM_TIM2_SetDuty_CH2(duty); }
void BMS_HW_Balance_S2(uint8_t duty) { PWM_TIM2_SetDuty_CH3(duty); }
