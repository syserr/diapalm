#include "diapalm.h"
#include "dpentry.h"
#include "dplist.h"
#include "dpedit.h"
#include "dpexport.h"
#include "dpalarm.h"
#include "dpa1c.h"
#include "resource.h"

/* Our own event type, used to hand an alarm to the live event loop. Any
   value in the user range is ours to use; the OS never generates one. */
#define DP_ALARM_EVENT ((eventsEnum)(firstUserEvent + 1))

/***********************************************************************
 * Menu commands
 ***********************************************************************/

static void ShowCountAlert(UInt16 alertId, UInt16 number) {
    Char text[maxStrIToALen];

    StrIToA(text, (Int32)number);
    FrmCustomAlert(alertId, text, "", "");
}

static void AppNewEntry(void) { DpEditOpen(DP_NEW_ENTRY); }

static void AppEditEntry(void) {
    UInt16 index = DpListSelectedIndex();

    if (index == DP_NEW_ENTRY) {
        FrmAlert(NoSelectionAlertId);
        return;
    }

    DpEditOpen(index);
}

static void AppDeleteEntry(void) {
    UInt16 index = DpListSelectedIndex();

    if (index == DP_NEW_ENTRY) {
        FrmAlert(NoSelectionAlertId);
        return;
    }

    if (FrmAlert(DeleteAlertId) != 0) return; /* 0 = Delete, 1 = Cancel */

    DpDelete(index);

    /* Redraw the list in place: post an open event to the live form. */
    {
        EventType event;

        MemSet(&event, sizeof(event), 0);
        event.eType = frmOpenEvent;
        event.data.frmOpen.formID = ListFormId;
        FrmDispatchEvent(&event);
    }
}

static void AppExport(void) {
    UInt16 rows = DpExportCsv();

    if (rows == dmMaxRecordIndex) {
        FrmAlert(ExportFailedAlertId);
        return;
    }

    ShowCountAlert(ExportedAlertId, rows);
}

static void AppSetReminder(void) {
    DateTimeType dt;
    Int16 hour;
    Int16 minute;
    UInt32 now = TimGetSeconds();
    UInt32 when;

    TimSecondsToDateTime(now, &dt);
    hour = dt.hour;
    minute = dt.minute;

    if (!SelectOneTime(&hour, &minute, "Remind me at")) return;

    /* SelectOneTime returns a time of day; if that has already passed today,
       the reminder belongs to tomorrow. */
    dt.hour = (Int16)hour;
    dt.minute = (Int16)minute;
    dt.second = 0;
    when = TimDateTimeToSeconds(&dt);
    if (when <= now) when += (UInt32)24 * 60 * 60;

    if (!DpAlarmSet(when)) {
        FrmAlert(ReminderFailedAlertId);
        return;
    }

    FrmAlert(ReminderSetAlertId);
}

static void AppCancelReminder(void) {
    DpAlarmSet(0);
    FrmAlert(ReminderClearedAlertId);
}

/***********************************************************************
 * Event plumbing
 ***********************************************************************/

static Boolean AppHandleEvent(EventPtr eventP) {
    UInt16 formId;
    FormType *formP;

    if (eventP->eType == frmLoadEvent) {
        formId = eventP->data.frmLoad.formID;
        formP = FrmInitForm(formId);
        FrmSetActiveForm(formP);

        switch (formId) {
            case ListFormId:
                FrmSetEventHandler(formP, DpListFormHandleEvent);
                break;
            case EditFormId:
                FrmSetEventHandler(formP, DpEditFormHandleEvent);
                break;
            case A1cFormId:
                FrmSetEventHandler(formP, DpA1cFormHandleEvent);
                break;
            default:
                break;
        }

        return true;
    }

    return false;
}

static void AppMenuEvent(EventPtr eventP) {
    switch (eventP->data.menu.itemID) {
        case NewEntryItemId:
            AppNewEntry();
            break;
        case ExportItemId:
            AppExport();
            break;
        case A1cItemId:
            DpA1cOpen();
            break;
        case EditEntryItemId:
            AppEditEntry();
            break;
        case DeleteEntryItemId:
            AppDeleteEntry();
            break;
        case SetReminderItemId:
            AppSetReminder();
            break;
        case CancelReminderItemId:
            AppCancelReminder();
            break;
        case AboutItemId:
            FrmAlert(AboutAlertId);
            break;
        default:
            break;
    }
}

/* "Cancel Reminder" is only meaningful while one is set, so the two reminder
   items trade places whenever a form comes up. Forms call this on open. */
void DpSyncMenu(void) {
    if (DpAlarmTime() != 0) {
        MenuShowItem(CancelReminderItemId);
        MenuHideItem(SetReminderItemId);
    } else {
        MenuHideItem(CancelReminderItemId);
        MenuShowItem(SetReminderItemId);
    }
}

static void AppEventLoop(void) {
    EventType event;
    UInt16 error = errNone;

    do {
        EvtGetEvent(&event, evtWaitForever);

        if (SysHandleEvent(&event)) continue;
        if (MenuHandleEvent(NULL, &event, &error)) continue;

        /* An alarm handed over by the subcall path in PilotMain. Handled here
           so it lands in the one live event loop rather than a nested one. */
        if (event.eType == DP_ALARM_EVENT) {
            DpEditOpen(DP_NEW_ENTRY);
            continue;
        }

        /* MenuHandleEvent consumes the open/track traffic; a selected item
           arrives as a menuEvent it deliberately leaves for us. */
        if (event.eType == menuEvent) {
            AppMenuEvent(&event);
            continue;
        }

        if (!AppHandleEvent(&event)) {
            FrmDispatchEvent(&event);
        }
    } while (event.eType != appStopEvent);
}

UInt32 PilotMain(UInt16 command, void *commandPBP, UInt16 launchFlags) {
    UInt32 result = errNone;

    (void)commandPBP;

    /* An alarm that comes due while Diapalm is already running is delivered
       as a direct call into this function on top of our own event loop -- the
       OS does not launch us again, it just calls us. Running the launch path
       here would allocate a second state chunk, open the database a second
       time, start a nested event loop, and then free the chunk the outer loop
       is still using. Post to the live loop instead and return. */
    if (launchFlags & sysAppLaunchFlagSubCall) {
        if (command == sysAppLaunchCmdDisplayAlarm) {
            EventType event;

            MemSet(&event, sizeof(event), 0);
            event.eType = DP_ALARM_EVENT;
            EvtAddEventToQueue(&event);
        }

        return errNone;
    }

    /* Every launch code needs the state chunk in place, because the entry,
       list, edit and alarm modules all reach for it. */
    DpStateInit();

    if (command != sysAppLaunchCmdNormalLaunch &&
        command != sysAppLaunchCmdDisplayAlarm) {
        DpStateExit();
        return errNone;
    }

    if (DpDbOpen() == NULL) {
        DpStateExit();
        SysFatalAlert("Diapalm: cannot open the database");
    }

    if (command == sysAppLaunchCmdDisplayAlarm) {
        /* The reminder fired and Diapalm was not running: drop straight into
           a fresh entry so the reading can be logged while it is in hand.
           (The already-running case never reaches here -- it is handled by the
           subcall branch above.) */
        DpEditOpen(DP_NEW_ENTRY);
    } else {
        FrmGotoForm(ListFormId);
#ifdef DP_AUTOTEST_ALARM
        /* Test hook: arm the reminder 2 minutes out with no user input, so
           the alarm path can be exercised without driving the emulator's UI
           (pen delivery there is unreliable). No confirmation alert: FrmAlert
           blocks until a tap, and the harness taps nothing, so the app would
           still be sitting in the modal when the alarm fired. Never defined
           in a real build. */
        DpAlarmSet(TimGetSeconds() + 120);
#endif
    }

    AppEventLoop();

    FrmCloseAllForms();
    DpDbClose();
    DpA1cLabClose();
    DpStateExit();

    return result;
}
