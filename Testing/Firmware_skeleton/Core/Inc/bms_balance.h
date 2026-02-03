//bms_balance.h
#ifndef BMS_BALANCE_H_
#define BMS_BALANCE_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  BAL_IDLE=0,
  BAL_CHARGE_CAP,
  BAL_DEADTIME,
  BAL_DISCHARGE_CAP
} bal_state_t;

typedef struct {
  bal_state_t st;
  uint32_t t_mark_ms;
} bal_ctx_t;

void BAL_Init(bal_ctx_t *b);
void BAL_Stop(bal_ctx_t *b);
void BAL_Tick(bal_ctx_t *b, uint32_t now_ms, bool enable);


#endif /* BMS_BALANCE_H_ */
