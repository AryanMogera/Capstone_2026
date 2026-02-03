//bms_config.h

#ifndef BMS_CONFIG_H_
#define BMS_CONFIG_H_


// Limits (have to tune later)
#define OV_LIMIT_V         3.90f
#define UV_LIMIT_V         2.00f
#define OT_LIMIT_C         60.0f
#define OC_LIMIT_A         20.0f

// Balancing
#define DV_START_V         0.030f
#define DV_STOP_V          0.010f
#define BAL_MAX_TEMP_C     50.0f
#define BAL_MAX_CURR_A     1.0f

// Deep discharge
#define DEEP_UV_LIMIT_V    1.50f	// "lower configurable UV cutoff"
#define DEEP_ENABLE_REQ    1		// require a "lab enable" command

// Timing (ms)
#define MEASURE_PERIOD_MS  50
#define TELEMETRY_MS       200
#define BAL_TCHARGE_MS     2
#define BAL_TDISCH_MS      2
#define BAL_DEAD_US        5       // microseconds deadtime (software)


#endif /* BMS_CONFIG_H_ */
