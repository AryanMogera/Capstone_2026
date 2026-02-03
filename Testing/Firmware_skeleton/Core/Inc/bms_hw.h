//bms_hw.h
#ifndef BMS_HW_H_
#define BMS_HW_H_

#include <stdint.h>
#include <stdbool.h>

void BMS_HW_Init(void);

// charge/discharge switches (placeholders for later)
void BMS_HW_SetChargeEnable(bool on);
void BMS_HW_SetDischargeEnable(bool on);

// balancing switches S1/S2 are PWM outputs
void BMS_HW_Balance_AllOff(void);
void BMS_HW_Balance_S1(uint8_t duty);
void BMS_HW_Balance_S2(uint8_t duty);


#endif /* BMS_HW_H_ */
