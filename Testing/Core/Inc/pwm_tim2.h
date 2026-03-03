//pwm_tim2.h
#ifndef PWM_TIM2_H_
#define PWM_TIM2_H_

#include <stdint.h>

void PWM_TIM2_Init(uint32_t tim_clk_hz, uint32_t pwm_hz);
void PWM_TIM2_SetDuty_CH2(uint8_t duty_percent); // S1
void PWM_TIM2_SetDuty_CH3(uint8_t duty_percent); // S2
void PWM_TIM2_AllOff(void);


#endif /* PWM_TIM2_H_ */
