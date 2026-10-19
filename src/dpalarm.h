/* One-shot "log your glucose" reminder built on the system alarm table. */

#ifndef DIAPALM_ALARM_H
#define DIAPALM_ALARM_H

#include <PalmOS.h>

/* Reference value echoed back in the alarm parameter block. */
#define DP_ALARM_REF 'DPa1'

/* Seconds-since-1904 at which the reminder is set, or 0 when none is set. */
UInt32 DpAlarmTime(void);

/* Sets (or, with 0, clears) the reminder. Returns true on success. */
Boolean DpAlarmSet(UInt32 whenSeconds);

#endif /* DIAPALM_ALARM_H */
