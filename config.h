// Configuration

// Simple throttle failover. If enabled, a watchdog is configured, which is
// reset when valid value is read from the throttle pin. If watchdog interrupt
// occurs, error status is set and mixer shutdowns the engines until signal
// re-appears plus few 20ms periods more.
//#define FO_ENABLED

// Watchdog period. The longer is period, the longer it takes for the mixer
// to detect signal loss (and harder it becomes, due to the noises), the
// mixer more tolerant to occasional signal loss.
#define FO_PERIOD   WDTO_60MS

// Amount of 20ms periods to wait after signal was restored. The bigger
// this number is, the longer it takes to recover. The smaller is number,
// the more prone mixer is to the noise when signal is lost. Basically,
// FO_WAIT * 20ms should be greater than FO_PERIOD. Otherwise, when signal
// is lost, single noise pulse that fits into 1-2ms can be considered as
// a signal.
#define FO_WAIT     5



// Invert rudder. Can be used when you don't want to change gyro direction.
// Note that in that case you may need to invert rudder channel on radio too.
//#define RUD_INVERT



// Capture values for 5 seconds (250 * 20ms) when calibrating
#define CALIBRATE_PERIODS 	250
