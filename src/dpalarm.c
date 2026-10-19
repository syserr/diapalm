#include "dpalarm.h"
#include "diapalm.h"
#include "dpentry.h"

/* An alarm is registered against the APPLICATION database -- the 'appl'
   database that SysAppLaunch hands back to us as sysAppLaunchCmdDisplayAlarm.
   It is NOT keyed on our entries database: passing that LocalID registers
   successfully (the alarm table stores the number without checking it) but
   the launch code is sent to whatever database owns that LocalID, so nothing
   ever reaches our PilotMain. DmGetNextDatabaseByTypeCreator is the same
   lookup the SDK's own AppCallWithCommand uses. */

static Boolean DpAppDbId(UInt16 *cardNoP, LocalID *dbIdP) {
    DmSearchStateType searchState;

    *cardNoP = 0;
    *dbIdP = 0;

    if (DmGetNextDatabaseByTypeCreator(true, &searchState,
                                       sysFileTApplication, DP_DB_CREATOR,
                                       true, cardNoP, dbIdP) != errNone) {
        *dbIdP = 0;
    }

    return *dbIdP != 0;
}

UInt32 DpAlarmTime(void) {
    UInt16 cardNo;
    LocalID dbId;
    UInt32 ref = 0;
    UInt32 when;

    if (!DpAppDbId(&cardNo, &dbId)) return 0;

    when = AlmGetAlarm(cardNo, dbId, &ref);
    if (ref != DP_ALARM_REF) return 0;

    return when;
}

Boolean DpAlarmSet(UInt32 whenSeconds) {
    UInt16 cardNo;
    LocalID dbId;
    Err err;

    if (!DpAppDbId(&cardNo, &dbId)) return false;

    if (whenSeconds == 0) {
        /* Clearing an alarm that was never set is not an error. */
        err = AlmSetAlarm(cardNo, dbId, DP_ALARM_REF, 0, true);
        return err == errNone;
    }

    err = AlmSetAlarm(cardNo, dbId, DP_ALARM_REF, whenSeconds, false);
    return err == errNone;
}
