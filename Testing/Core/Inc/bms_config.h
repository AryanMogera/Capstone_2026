// Per-series-group voltage limits (4S pack uses 4 groups)
// Datasheet charge CV is 4.10 ± 0.05 V  -> hard max = 4.15 V
#define OV_LIMIT_V         4.15f

// Datasheet discharge cutoff is 1.50 ± 0.05 V
// For normal operation, stop higher than absolute minimum
#define UV_LIMIT_V         1.70f

// Deep discharge mode (only if intentionally allow going lower)
#define DEEP_UV_LIMIT_V    1.50f

// Temperature
// Datasheet discharge max is 60C; charge max is 45C.
// If you only keep ONE OT limit, keep it conservative:
#define OT_LIMIT_C         60.0f   // ok for discharge, but you should add charge-specific later

// Current (pack current) --------
// 1500 mAh cell => 1C = 1.5A per cell. With 2P, current capability roughly doubles.
// Use a safe bring-up value until you confirm your sense scaling.
#define OC_LIMIT_A         6.0f    // conservative hard fault for 2P pack bring-up

// Balancing
#define DV_START_V         0.030f
#define DV_STOP_V          0.010f
#define BAL_MAX_TEMP_C     45.0f   // keep balancing in charge-safe temp range
#define BAL_MAX_CURR_A     1.0f


// Timing
#define MEASURE_PERIOD_MS  50
#define TELEMETRY_MS       50

// Charger detect ADC thresholds (ADC pin voltage, NOT charger voltage)
#define CHG_DET_ON_V   1.80f   // above this = charger connected
#define CHG_DET_OFF_V  1.60f   // below this = charger disconnected
