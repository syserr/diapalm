#include "dpedit.h"
#include "diapalm.h"
#include "dpentry.h"
#include "dpalarm.h"
#include "resource.h"

#define DP_EDIT_NEXT DP_NEW_ENTRY

static FieldType *EditField(FormType *formP, UInt16 id) {
    return (FieldType *)FrmGetObjectPtr(formP,
                                        FrmGetObjectIndex(formP, id));
}

static ControlType *EditControl(FormType *formP, UInt16 id) {
    return (ControlType *)FrmGetObjectPtr(formP,
                                          FrmGetObjectIndex(formP, id));
}

/* Field text helpers. Fields own heap blocks; FldSetTextHandle transfers
   ownership to the field, which frees it when the form closes. */
static void EditSetText(FormType *formP, UInt16 id, const Char *textP) {
    FieldType *fldP = EditField(formP, id);
    UInt16 len = StrLen(textP);
    MemHandle h = MemHandleNew(len + 1);

    if (h == NULL) return;

    StrCopy((Char *)MemHandleLock(h), textP);
    MemHandleUnlock(h);

    FldSetTextHandle(fldP, h);
    FldDrawField(fldP);
}

static const Char *EditGetText(FormType *formP, UInt16 id) {
    const Char *textP = FldGetTextPtr(EditField(formP, id));

    return (textP == NULL) ? "" : textP;
}

static void EditRefreshWhen(void) {
    FormType *formP = FrmGetActiveForm();
    ControlType *ctlP = EditControl(formP, EditFormSetTimeButtonId);
    Char *when = DpStateGet()->labelText;

    DpFormatWhen(DpStateGet()->editWhen, when, DP_LABEL_MAX);
    CtlSetLabel(ctlP, when);
    CtlDrawControl(ctlP);
}

static void EditLoadForm(FormType *formP) {
    DpState *st = DpStateGet();
    DiapalmEntry entry;
    Char num[12];
    Boolean editing = (st->editIndex != DP_NEW_ENTRY);

    MemSet(&entry, sizeof(entry), 0);
    entry.timestamp = st->editWhen;

    if (editing && DpGet(st->editIndex, &entry)) {
        st->editWhen = entry.timestamp;
    } else {
        st->editIndex = DP_NEW_ENTRY;
        editing = false;
    }

    EditSetText(formP, EditFormNoteId, entry.note);

    DpFormatUInt(entry.bloodSugarMgDl, num, sizeof(num));
    EditSetText(formP, EditFormBgFieldId, num);

    DpFormatInsulin(entry.insulinTenths, num, sizeof(num));
    EditSetText(formP, EditFormInsulinFieldId, num);

    DpFormatUInt(entry.mealCarbsG, num, sizeof(num));
    EditSetText(formP, EditFormCarbsFieldId, num);

    /* Delete only makes sense for a record that exists. */
    if (editing) {
        FrmShowObject(formP, FrmGetObjectIndex(formP, EditFormDeleteButtonId));
    } else {
        FrmHideObject(formP, FrmGetObjectIndex(formP, EditFormDeleteButtonId));
    }

    EditRefreshWhen();
}

static Boolean EditSave(void) {
    DpState *st = DpStateGet();
    FormType *formP = FrmGetActiveForm();
    DiapalmEntry entry;
    UInt16 bg;
    UInt16 insulin;
    UInt16 carbs;

    MemSet(&entry, sizeof(entry), 0);

    entry.timestamp = st->editWhen;
    bg = DpParseUInt(EditGetText(formP, EditFormBgFieldId));
    insulin = DpParseTenths(EditGetText(formP, EditFormInsulinFieldId));
    carbs = DpParseUInt(EditGetText(formP, EditFormCarbsFieldId));

    if (bg > 999) {
        FrmCustomAlert(ErrorAlertId, "Blood sugar must be 0-999 mg/dL.", "", "");
        return false;
    }
    if (insulin > 999) {
        FrmCustomAlert(ErrorAlertId, "Insulin must be 0.1-99.9 units.", "", "");
        return false;
    }
    if (carbs > 999) {
        FrmCustomAlert(ErrorAlertId, "Carbs must be 0-999 grams.", "", "");
        return false;
    }

    if (bg == 0 && insulin == 0 && carbs == 0 &&
        EditGetText(formP, EditFormNoteId)[0] == '\0') {
        FrmAlert(NothingToSaveAlertId);
        return false;
    }

    entry.bloodSugarMgDl = bg;
    entry.insulinTenths = insulin;
    entry.mealCarbsG = carbs;
    StrNCopy(entry.note, EditGetText(formP, EditFormNoteId), DP_NOTE_MAX - 1);

    if (st->editIndex == DP_NEW_ENTRY) {
        if (DpAdd(&entry) == dmMaxRecordIndex) {
            FrmCustomAlert(ErrorAlertId, "Could not save the entry.", "", "");
            return false;
        }
    } else if (!DpReplace(st->editIndex, &entry)) {
        FrmCustomAlert(ErrorAlertId, "Could not save the entry.", "", "");
        return false;
    }

    return true;
}

static void EditChangeTime(void) {
    DpState *st = DpStateGet();
    DateTimeType dt;
    Int16 month;
    Int16 day;
    Int16 year;

    TimSecondsToDateTime(st->editWhen, &dt);
    month = dt.month;
    day = dt.day;
    year = dt.year;

    if (!SelectDay(selectDayByDay, &month, &day, &year, "Entry Date")) return;

    dt.month = (Int16)month;
    dt.day = (Int16)day;
    dt.year = (Int16)year;
    st->editWhen = TimDateTimeToSeconds(&dt);

    EditRefreshWhen();
}

static void EditDelete(void) {
    DpState *st = DpStateGet();

    if (st->editIndex == DP_NEW_ENTRY) return;

    if (FrmAlert(DeleteAlertId) != 0) return; /* 0 = Delete, 1 = Cancel */

    DpDelete(st->editIndex);
    st->editIndex = DP_NEW_ENTRY;
    FrmGotoForm(ListFormId);
}

void DpEditOpen(UInt16 storageIndex) {
    DpState *st = DpStateGet();

    if (storageIndex == DP_NEW_ENTRY) {
        st->editIndex = DP_NEW_ENTRY;
        st->editWhen = TimGetSeconds();
    } else {
        st->editIndex = storageIndex;
    }

    FrmGotoForm(EditFormId);
}

/* The numeric fields are small for a finger or a worn stylus, and a tap on
   their label does nothing by default. Treat a tap anywhere in the field's
   row band, from the previous column to the next, as a tap on the field. */
static Boolean EditFocusNearTap(FormType *formP, Int16 x, Int16 y) {
    static const struct { UInt16 id; Int16 left; Int16 right; } zones[] = {
        { EditFormBgFieldId,      0,  60 },
        { EditFormInsulinFieldId, 60, 160 },
        { EditFormCarbsFieldId,   0,  160 },
    };
    UInt16 i;

    for (i = 0; i < sizeof(zones) / sizeof(zones[0]); i++) {
        UInt16 idx = FrmGetObjectIndex(formP, zones[i].id);
        RectangleType r;

        FrmGetObjectBounds(formP, idx, &r);
        if (RctPtInRectangle(x, y, &r)) return false; /* field handles it */

        r.topLeft.x = zones[i].left;
        r.extent.x = zones[i].right - zones[i].left;
        r.topLeft.y -= 6;
        r.extent.y += 12;
        if (RctPtInRectangle(x, y, &r)) {
            FrmSetFocus(formP, idx);
            return true;
        }
    }
    return false;
}

Boolean DpEditFormHandleEvent(EventPtr eventP) {
    switch (eventP->eType) {
        case frmOpenEvent:
            FrmDrawForm(FrmGetActiveForm());
            EditLoadForm(FrmGetActiveForm());
            return true;

        case penDownEvent:
            return EditFocusNearTap(FrmGetActiveForm(), eventP->screenX,
                                    eventP->screenY);

        case ctlSelectEvent:
            switch (eventP->data.ctlSelect.controlID) {
                case EditFormSetTimeButtonId:
                    EditChangeTime();
                    return true;

                case EditFormOkButtonId:
                    if (EditSave()) {
                        DpStateGet()->editIndex = DP_NEW_ENTRY;
                        FrmGotoForm(ListFormId);
                    }
                    return true;

                case EditFormCancelButtonId:
                    DpStateGet()->editIndex = DP_NEW_ENTRY;
                    FrmGotoForm(ListFormId);
                    return true;

                case EditFormDeleteButtonId:
                    EditDelete();
                    return true;

                default:
                    break;
            }
            break;

        case frmCloseEvent:
            /* Drop the text blocks the fields own before the form goes. */
            FldFreeMemory(EditField(FrmGetActiveForm(), EditFormNoteId));
            FldFreeMemory(EditField(FrmGetActiveForm(), EditFormBgFieldId));
            FldFreeMemory(EditField(FrmGetActiveForm(), EditFormInsulinFieldId));
            FldFreeMemory(EditField(FrmGetActiveForm(), EditFormCarbsFieldId));
            break;

        default:
            break;
    }

    return false;
}
