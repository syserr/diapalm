#include "diapalm.h"

/* Application state lives in the dynamic heap because the 68K code resource
   is read-only and the toolchain links static storage out of the image. The
   chunk address is published in a feature slot so every module can find it.

   Each launch starts with a fresh chunk: the previous launch's chunk died
   with its dynamic heap, so a leftover slot value must never be dereferenced.
   DpStateInit() always overwrites the slot, and PilotMain calls it before
   anything else can ask for the state. */

void DpStateInit(void) {
    DpState *st = (DpState *)MemPtrNew(sizeof(DpState));

    if (st == NULL) {
        SysFatalAlert("Diapalm: out of memory");
    }

    MemSet(st, sizeof(DpState), 0);
    st->magic = DP_STATE_MAGIC;
    st->db = NULL;
    st->dbId = 0;
    st->editIndex = DP_NEW_ENTRY;
    st->editWhen = TimGetSeconds();
    st->alarmLaunch = false;
    st->listRowsH = NULL;
    st->labDb = NULL;
    st->labDbId = 0;
    st->a1cWhen = TimGetSeconds();
    st->a1cRowsH = NULL;

    if (FtrSet(DP_FEATURE_CREATOR, DP_FEATURE_NUM, (UInt32)st) != errNone) {
        MemPtrFree(st);
        SysFatalAlert("Diapalm: cannot store state");
    }
}

DpState *DpStateGet(void) {
    UInt32 value = 0;

    if (FtrGet(DP_FEATURE_CREATOR, DP_FEATURE_NUM, &value) != errNone ||
        value == 0) {
        DpStateInit();
        FtrGet(DP_FEATURE_CREATOR, DP_FEATURE_NUM, &value);
    }

    return (DpState *)value;
}

void DpStateExit(void) {
    UInt32 value = 0;

    if (FtrGet(DP_FEATURE_CREATOR, DP_FEATURE_NUM, &value) == errNone &&
        value != 0) {
        FtrUnregister(DP_FEATURE_CREATOR, DP_FEATURE_NUM);
        MemPtrFree((void *)value);
    }
}
