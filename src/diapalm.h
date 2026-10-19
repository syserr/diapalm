/* Shared app-wide definitions for Diapalm. */

#ifndef DIAPALM_DIAPALM_H
#define DIAPALM_DIAPALM_H

#include <PalmOS.h>

/* ---------------------------------------------------------- module state */

/* The Palm 68K code resource is read-only at run time, and the toolchain
   links static storage out of the image entirely, so ordinary C globals are
   not available. Application state lives in a heap chunk whose address is
   parked in a feature slot for the lifetime of one launch. */
#define DP_FEATURE_CREATOR 'dprd'
#define DP_FEATURE_NUM     1
#define DP_STATE_MAGIC     0xD1A6U

/* Longest text handed to CtlSetLabel: the edit form's "10/18 11:30a" (14
   characters) and the A1c form's "10/18/2026" (10). */
#define DP_LABEL_MAX 24

typedef struct {
    UInt32 magic;
    DmOpenRef db;       /* open database, NULL when closed */
    LocalID dbId;       /* local id, 0 when closed */
    UInt16 editIndex;   /* record being edited, dmMaxRecordIndex = new entry */
    UInt32 editWhen;    /* timestamp shown on the edit form */
    Boolean alarmLaunch;/* launched by sysAppLaunchCmdDisplayAlarm */
    MemHandle listRowsH;/* row strings handed to the list, NULL when none */
    DmOpenRef labDb;    /* open A1c lab-value database, NULL when closed */
    LocalID labDbId;    /* its local id, 0 when closed */
    UInt32 a1cWhen;     /* date shown on the A1c form for a new lab value */
    MemHandle a1cRowsH; /* row strings handed to the A1c list */
    /* CtlSetLabel keeps the pointer it is given, so the text has to outlive
       the call. A form local would dangle as soon as the drawing function
       returned, and anything running afterwards (SelectDay, then the redraw)
       reuses that stack. Only one form is on screen at a time, so the edit
       and A1c date buttons share this one buffer. */
    Char labelText[DP_LABEL_MAX];
} DpState;

/* Allocates a fresh state chunk and publishes it. Call once, first thing,
   in PilotMain for every launch code. */
void DpStateInit(void);

/* Returns the state published by DpStateInit(). */
DpState *DpStateGet(void);

/* Unpublishes and frees the state chunk. Call on the way out of PilotMain. */
void DpStateExit(void);

#define DP_NEW_ENTRY dmMaxRecordIndex

/* Shows "Cancel Reminder" in place of "Set Reminder..." while a reminder is
   pending. Forms call this when they open, because the menu items belong to
   the form that is coming up. */
void DpSyncMenu(void);

#endif /* DIAPALM_DIAPALM_H */
