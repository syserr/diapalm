#include "dpa1c.h"
#include "diapalm.h"
#include "dpentry.h"
#include "resource.h"

/***********************************************************************
 * The estimate
 ***********************************************************************/

/* The Android app's estimate (shared/Helper.java):

       percent = 0.031f * avgMgDl + 2.393f

   over the average blood sugar of the last three months. Rewritten for a
   machine with no floating point, in tenths of a percent:

       tenths = 10 * (0.031 * avgMgDl + 2.393) = (31 * avgMgDl + 2393) / 100

   Rounding the average first and then the result would round twice, so the
   sum is carried straight through instead and both steps fold into one
   division (avg = sum / n):

       tenths = (31 * sum + 2393 * n) / (100 * n)

   A record count is a UInt16, so the numerator's worst case is
   31 * 999 * 65535 + 2393 * 65535, about 2.2e9 -- inside a UInt32 with room
   to spare, provided the accumulated sum is bounded. Readings are clamped to
   DP_A1C_MAX_MGDL as they are added (see below), which is what makes that
   bound hold for a record written by anything, not just by this app.

   Note the 2393, not 2443: the constant is hundredths of a percent, and the
   two differ by 0.05%, which is invisible at a single reading and a whole
   tenth out at an average one. */

/* Highest reading the estimate will count, in mg/dL. The entry form already
   refuses anything above 999; the clamp is here so a record that arrived some
   other way (a hand-built PDB, a future format) cannot overflow the sum. */
#define DP_A1C_MAX_MGDL 999

UInt16 DpA1cEstimate(UInt16 *avgMgDlP, UInt16 *readingCountP) {
    UInt32 now = TimGetSeconds();
    UInt32 windowStart;
    UInt32 sum = 0;
    UInt16 count = 0;
    UInt16 total = DpCount();
    UInt16 i;
    UInt32 numerator;
    UInt32 denominator;

    if (avgMgDlP != NULL) *avgMgDlP = 0;
    if (readingCountP != NULL) *readingCountP = 0;

    /* 90 days back. Guard the subtraction: a device clock set near the epoch
       would otherwise wrap into the far future and select nothing. */
    {
        UInt32 span = (UInt32)DP_A1C_WINDOW_DAYS * 24UL * 60UL * 60UL;

        windowStart = (now > span) ? (now - span) : 0;
    }

    for (i = 0; i < total; i++) {
        DiapalmEntry entry;

        if (!DpGet(i, &entry)) continue;

        /* Entries with no reading are skipped, and so are entries dated in
           the future -- the Android interval is half-open at now. */
        if (entry.bloodSugarMgDl == 0) continue;
        if (entry.timestamp < windowStart || entry.timestamp > now) continue;

        sum += (entry.bloodSugarMgDl > DP_A1C_MAX_MGDL)
                   ? DP_A1C_MAX_MGDL
                   : entry.bloodSugarMgDl;
        count++;
    }

    if (count == 0) return 0;

    if (avgMgDlP != NULL) {
        *avgMgDlP = (UInt16)((sum + count / 2) / count);
    }
    if (readingCountP != NULL) *readingCountP = count;

    numerator = 31UL * sum + 2393UL * count;
    denominator = 100UL * count;

    return (UInt16)((numerator + denominator / 2) / denominator);
}

/***********************************************************************
 * Lab values
 ***********************************************************************/

/* One dated blood-draw result. Both fields are 32-bit on purpose: mixing a
   UInt32 and a UInt16 pair would leave the record's size to the compiler's
   padding rules, and this struct is written to disk verbatim. */
typedef struct {
    UInt32 timestamp;
    UInt32 tenths;
} DpA1cLabEntry;

typedef char DpA1cLabSizeCheck[(sizeof(DpA1cLabEntry) == 8) ? 1 : -1];

static Int16 DpA1cLabCompare(void *rec1P, void *rec2P, Int16 other,
                             SortRecordInfoPtr rec1InfoP,
                             SortRecordInfoPtr rec2InfoP,
                             MemHandle appInfoH) {
    const DpA1cLabEntry *a = (const DpA1cLabEntry *)rec1P;
    const DpA1cLabEntry *b = (const DpA1cLabEntry *)rec2P;

    (void)other;
    (void)rec1InfoP;
    (void)rec2InfoP;
    (void)appInfoH;

    if (a->timestamp < b->timestamp) return -1;
    if (a->timestamp > b->timestamp) return 1;
    return 0;
}

/* True when the database is ours. A name match alone is not enough: another
   database wearing our name would be corrupted by our writes. */
static Boolean DpA1cLabIsOurs(UInt16 cardNo, LocalID dbID) {
    UInt32 type = 0;
    UInt32 creator = 0;

    if (DmDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, &type, &creator) != errNone) {
        return false;
    }

    return type == DP_A1C_DB_TYPE && creator == DP_DB_CREATOR;
}

DmOpenRef DpA1cLabOpen(void) {
    DpState *st = DpStateGet();
    UInt16 cardNo = 0;
    LocalID dbID;

    if (st->labDb != NULL) return st->labDb;

    dbID = DmFindDatabase(cardNo, DP_A1C_DB_NAME);

    if (dbID != 0 && !DpA1cLabIsOurs(cardNo, dbID)) {
        dbID = 0;
    }

    if (dbID == 0) {
        if (DmCreateDatabase(cardNo, DP_A1C_DB_NAME, DP_DB_CREATOR,
                             DP_A1C_DB_TYPE, false) != errNone) {
            return NULL;
        }

        dbID = DmFindDatabase(cardNo, DP_A1C_DB_NAME);
        if (dbID == 0) return NULL;
    }

    st->labDbId = dbID;
    st->labDb = DmOpenDatabase(cardNo, dbID, dmModeReadWrite);
    if (st->labDb == NULL) {
        st->labDbId = 0;
        return NULL;
    }

    (void)DmInsertionSort(st->labDb, DpA1cLabCompare, 0);

    return st->labDb;
}

void DpA1cLabClose(void) {
    DpState *st = DpStateGet();

    if (st->labDb != NULL) {
        DmCloseDatabase(st->labDb);
        st->labDb = NULL;
    }
    st->labDbId = 0;
}

UInt16 DpA1cLabCount(void) {
    DmOpenRef db = DpA1cLabOpen();

    return (db == NULL) ? 0 : DmNumRecords(db);
}

Boolean DpA1cLabGet(UInt16 index, UInt32 *whenP, UInt16 *tenthsP) {
    DmOpenRef db = DpA1cLabOpen();
    MemHandle h;
    DpA1cLabEntry *recP;

    if (whenP != NULL) *whenP = 0;
    if (tenthsP != NULL) *tenthsP = 0;

    if (db == NULL || index >= DmNumRecords(db)) return false;

    h = DmQueryRecord(db, index);
    if (h == NULL) return false;

    recP = (DpA1cLabEntry *)MemHandleLock(h);
    if (recP == NULL) return false;

    if (whenP != NULL) *whenP = recP->timestamp;
    if (tenthsP != NULL) *tenthsP = (UInt16)recP->tenths;

    MemHandleUnlock(h);
    return true;
}

UInt16 DpA1cLabAdd(UInt32 when, UInt16 tenths) {
    DmOpenRef db = DpA1cLabOpen();
    DpA1cLabEntry rec;
    MemHandle h;
    DpA1cLabEntry *recP;
    UInt16 index;
    Err err;

    if (db == NULL) return dmMaxRecordIndex;
    if (tenths == 0) return dmMaxRecordIndex;

    rec.timestamp = when;
    rec.tenths = (UInt32)tenths;

    index = DmFindSortPosition(db, &rec, NULL, DpA1cLabCompare, 0);

    h = DmNewRecord(db, &index, sizeof(DpA1cLabEntry));
    if (h == NULL) return dmMaxRecordIndex;

    recP = (DpA1cLabEntry *)MemHandleLock(h);
    if (recP == NULL) {
        DmRemoveRecord(db, index);
        return dmMaxRecordIndex;
    }

    /* Record data goes through DmWrite: the memory manager may relocate the
       locked chunk, so a raw store through the pointer is not safe. */
    err = DmWrite(recP, 0, &rec, sizeof(DpA1cLabEntry));
    MemHandleUnlock(h);
    DmReleaseRecord(db, index, true);

    if (err != errNone) {
        DmRemoveRecord(db, index);
        return dmMaxRecordIndex;
    }

    return index;
}

Boolean DpA1cLabDelete(UInt16 index) {
    DmOpenRef db = DpA1cLabOpen();

    if (db == NULL || index >= DmNumRecords(db)) return false;
    return DmRemoveRecord(db, index) == errNone;
}

UInt16 DpA1cLabLatest(UInt32 *whenP) {
    DmOpenRef db = DpA1cLabOpen();
    UInt32 now = TimGetSeconds();
    Int32 i;

    if (whenP != NULL) *whenP = 0;
    if (db == NULL) return 0;

    /* Records are sorted oldest-first, so the newest at or before now is the
       first one found walking backwards from the end. */
    for (i = (Int32)DmNumRecords(db) - 1; i >= 0; i--) {
        MemHandle h = DmQueryRecord(db, (UInt16)i);
        DpA1cLabEntry *recP;
        UInt32 when;
        UInt32 tenths;

        if (h == NULL) continue;

        recP = (DpA1cLabEntry *)MemHandleLock(h);
        if (recP == NULL) continue;

        when = recP->timestamp;
        tenths = recP->tenths;
        MemHandleUnlock(h);

        if (when > now) continue;

        if (whenP != NULL) *whenP = when;
        return (UInt16)tenths;
    }

    return 0;
}

UInt16 DpA1cCurrent(Boolean *isLabP, UInt32 *whenP) {
    UInt32 now = TimGetSeconds();
    UInt32 fresh;
    UInt32 labWhen = 0;
    UInt16 lab;
    UInt16 est;

    if (isLabP != NULL) *isLabP = false;
    if (whenP != NULL) *whenP = 0;

    lab = DpA1cLabLatest(&labWhen);
    est = DpA1cEstimate(NULL, NULL);

    fresh = (UInt32)DP_A1C_LAB_DAYS * 24UL * 60UL * 60UL;

    /* A recent lab result is a measured value, so it outranks the estimate.
       Past DP_A1C_LAB_DAYS it stops speaking for today and the estimate takes
       over -- otherwise an old blood draw would mask the current average. */
    if (lab != 0 && labWhen != 0 && now >= labWhen && (now - labWhen) <= fresh) {
        if (isLabP != NULL) *isLabP = true;
        if (whenP != NULL) *whenP = labWhen;
        return lab;
    }

    if (est != 0) return est;

    /* No readings in the window at all. A stale lab value is still a real
       measurement of this person, so show it rather than nothing; the "lab"
       marker and its date keep it from reading as current. */
    if (lab != 0) {
        if (isLabP != NULL) *isLabP = true;
        if (whenP != NULL) *whenP = labWhen;
        return lab;
    }

    return 0;
}

/***********************************************************************
 * Formatting
 ***********************************************************************/

void DpA1cFormat(UInt16 tenths, Char *outP, UInt16 outSize) {
    if (outSize == 0) return;

    if (tenths == 0) {
        outP[0] = '\0';
        return;
    }

    StrPrintF(outP, "%d.%d", tenths / 10, tenths % 10);
}

void DpA1cFormatDate(UInt32 timestamp, Char *outP, UInt16 outSize) {
    DateTimeType dt;

    (void)outSize;

    TimSecondsToDateTime(timestamp, &dt);

    /* DateTimeType.year holds the full calendar year in this SDK (the entry
       form's "10/18/2026" comes from the same field), so it is used as-is. */
    StrPrintF(outP, "%d/%d/%d", dt.month, dt.day, (UInt16)dt.year);
}

Boolean DpA1cParsePercent(const Char *textP, UInt16 *tenthsP) {
    UInt32 whole = 0;
    UInt32 frac = 0;
    UInt32 result;
    Boolean anyDigit = false;

    if (tenthsP != NULL) *tenthsP = 0;
    if (textP == NULL) return false;

    while (*textP == ' ' || *textP == '\t') textP++;

    while (*textP >= '0' && *textP <= '9') {
        whole = whole * 10 + (UInt32)(*textP - '0');
        anyDigit = true;
        /* Past three digits the value is far outside the accepted range, so
           stop accumulating rather than wrapping. */
        if (whole > 999) return false;
        textP++;
    }

    if (*textP == '.' || *textP == ',') {
        textP++;
        if (*textP >= '0' && *textP <= '9') {
            frac = (UInt32)(*textP - '0');
            anyDigit = true;
            /* The digit has to be consumed as well: the end-of-string check
               below rejects anything still unconsumed, so leaving the pointer
               on the fraction digit would reject every decimal value. */
            textP++;
        }
    }

    while (*textP == ' ' || *textP == '\t') textP++;
    if (!anyDigit || *textP != '\0') return false;

    result = whole * 10 + frac;

    /* A result outside this range is a typo, not a reading. This is also what
       catches "68" typed for 6.8%: 680 tenths is not a person's A1c. */
    if (result < DP_A1C_MIN_TENTHS || result > DP_A1C_MAX_TENTHS) return false;

    if (tenthsP != NULL) *tenthsP = (UInt16)result;
    return true;
}

void DpA1cStatusText(Char *outP, UInt16 outSize) {
    Boolean isLab = false;
    UInt32 when = 0;
    UInt16 tenths;
    Char value[8];
    Char date[16];

    if (outSize == 0) return;
    outP[0] = '\0';

    tenths = DpA1cCurrent(&isLab, &when);
    DpA1cFormat(tenths, value, sizeof(value));

    if (tenths == 0) {
        StrNCopy(outP, "est. A1c --  no readings", (Int16)outSize - 1);
    } else if (isLab) {
        /* Say "lab" and give the draw's date: the marker is what keeps a
           hand-entered result from being read as an estimate. */
        DpA1cFormatDate(when, date, sizeof(date));
        StrPrintF(outP, "A1c %s%%  lab %s", value, date);
    } else {
        StrPrintF(outP, "est. A1c %s%%", value);
    }

    outP[outSize - 1] = '\0';
}

/***********************************************************************
 * The A1c form
 ***********************************************************************/

/* Two summary lines are drawn rather than placed as LABEL resources because
   their text is computed at open time; the form draws them itself, the way
   the list form draws its hint and status lines. */
#define DP_A1C_LINE_EST_Y 15
#define DP_A1C_LINE_LAB_Y 26
/* Where the list starts, matching A1cFormListId's AT (0 40 ...) in app.rcp. */
#define DP_A1C_LIST_TOP   40

/* WinDrawChars paints the glyphs and nothing else, so a shorter centred string
   leaves the tail of the longer one it replaced on screen: deleting the last
   lab value turned "lab 7.4%  10/18/2026" into "Ino lab value entered;". The
   two lines are cleared as one band before either is drawn. It stops just
   above the list frame at y 40, so the band is white form background and the
   frame survives. */
static void A1cEraseSummary(void) {
    RectangleType r;
    PatternType saved = WinGetPatternType();

    r.topLeft.x = 0;
    r.topLeft.y = DP_A1C_LINE_EST_Y;
    r.extent.x = 160;
    /* One row short of the list: row 39 is the frame's top border, and the
       erase should not have to rely on the redraw that follows to put it
       back. */
    r.extent.y = DP_A1C_LIST_TOP - DP_A1C_LINE_EST_Y - 1;

    /* Erasing paints with whatever pattern is current, so pin it to white
       rather than trusting what the last caller left behind. */
    WinSetPatternType(whitePattern);
    WinEraseRectangle(&r, 0);
    WinSetPatternType(saved);
}

static void A1cDrawLine(const Char *textP, Int16 y) {
    Int16 width;

    if (textP == NULL || textP[0] == '\0') return;

    width = FntCharsWidth(textP, StrLen(textP));
    WinDrawChars(textP, StrLen(textP), (160 - width) / 2, y);
}

/* "est. A1c 6.2%" plus how it was arrived at, and the newest lab value with
   its date. Both lines are always drawn, so the form shows what it knows even
   when one side has nothing. */
static void A1cDrawSummary(void) {
    UInt16 est;
    UInt16 avg = 0;
    UInt16 readings = 0;
    UInt16 lab;
    UInt32 labWhen = 0;
    Char line[40];
    Char value[8];

    A1cEraseSummary();

    est = DpA1cEstimate(&avg, &readings);
    DpA1cFormat(est, value, sizeof(value));

    if (est == 0) {
        StrNCopy(line, "est. A1c --  no readings", sizeof(line) - 1);
    } else {
        StrPrintF(line, "est. A1c %s%%  avg %d  n %d", value, avg, readings);
    }
    A1cDrawLine(line, DP_A1C_LINE_EST_Y);

    lab = DpA1cLabLatest(&labWhen);
    DpA1cFormat(lab, value, sizeof(value));

    if (lab == 0) {
        StrNCopy(line, "no lab value entered", sizeof(line) - 1);
    } else {
        Char date[16];

        DpA1cFormatDate(labWhen, date, sizeof(date));
        StrPrintF(line, "lab %s%%  %s", value, date);
    }
    A1cDrawLine(line, DP_A1C_LINE_LAB_Y);
}

static ListType *A1cList(FormType *formP) {
    return (ListType *)FrmGetObjectPtr(
        formP, FrmGetObjectIndex(formP, A1cFormListId));
}

static FieldType *A1cField(FormType *formP) {
    return (FieldType *)FrmGetObjectPtr(
        formP, FrmGetObjectIndex(formP, A1cFormValueFieldId));
}

/* Storage is oldest-first; the list shows newest-first, like the entry list. */
static Int16 A1cStorageIndex(Int16 itemNum) {
    return (Int16)(DpA1cLabCount() - 1 - (UInt16)itemNum);
}

static void A1cFreeRows(void) {
    DpState *st = DpStateGet();

    if (st->a1cRowsH != NULL) {
        MemHandleUnlock(st->a1cRowsH);
        MemHandleFree(st->a1cRowsH);
        st->a1cRowsH = NULL;
    }
}

static void A1cRefresh(FormType *formP) {
    ListType *listP = A1cList(formP);
    DpState *st = DpStateGet();
    UInt16 count = DpA1cLabCount();

    LstSetListChoices(listP, NULL, 0);
    A1cFreeRows();

    if (count > 0) {
        UInt16 size = (UInt16)(count * (sizeof(Char *) + DP_A1C_ROW_MAX));
        MemHandle h = MemHandleNew(size);

        if (h != NULL) {
            Char **items = (Char **)MemHandleLock(h);
            Char *pool = (Char *)(items + count);
            UInt16 i;

            for (i = 0; i < count; i++) {
                UInt32 when = 0;
                UInt16 tenths = 0;
                Char date[16];
                Char value[8];
                Char *row = pool;

                items[i] = row;
                row[0] = '\0';

                if (DpA1cLabGet((UInt16)A1cStorageIndex((Int16)i), &when,
                                &tenths)) {
                    DpA1cFormatDate(when, date, sizeof(date));
                    DpA1cFormat(tenths, value, sizeof(value));
                    StrPrintF(row, "%s    %s%%", date, value);
                }

                pool += DP_A1C_ROW_MAX;
            }

            st->a1cRowsH = h;
            LstSetListChoices(listP, items, (Int16)count);
            LstSetSelection(listP, 0);
        }
    } else {
        LstSetSelection(listP, noListSelection);
    }

    LstDrawList(listP);
}

static void A1cAddValue(void) {
    FormType *formP = FrmGetActiveForm();
    const Char *textP = FldGetTextPtr(A1cField(formP));
    UInt16 tenths = 0;
    DpState *st = DpStateGet();

    if (!DpA1cParsePercent(textP, &tenths)) {
        FrmCustomAlert(ErrorAlertId,
                       "Enter a lab A1c like 6.8 (2.0-25.0).", "", "");
        return;
    }

    if (DpA1cLabAdd(st->a1cWhen, tenths) == dmMaxRecordIndex) {
        FrmCustomAlert(ErrorAlertId, "Could not save the lab value.", "", "");
        return;
    }

    /* Clear the field so the same value is not entered twice by a double tap. */
    if (textP != NULL) {
        FldDelete(A1cField(formP), 0, StrLen(textP));
    }

    FrmDrawForm(formP);
    A1cDrawSummary();
    A1cRefresh(formP);
}

static void A1cDeleteValue(void) {
    FormType *formP = FrmGetActiveForm();
    ListType *listP = A1cList(formP);
    Int16 itemNum = LstGetSelection(listP);

    if (itemNum < 0) {
        FrmAlert(NoA1cSelectionAlertId);
        return;
    }

    if (FrmAlert(DeleteA1cAlertId) != 0) return; /* 0 = Delete, 1 = Cancel */

    DpA1cLabDelete((UInt16)A1cStorageIndex(itemNum));

    FrmDrawForm(formP);
    A1cDrawSummary();
    A1cRefresh(formP);
}

/* The button keeps the pointer it is handed, so the text comes from the state
   chunk rather than a local. */
static void A1cRefreshDateButton(void) {
    FormType *formP = FrmGetActiveForm();
    ControlType *ctlP = (ControlType *)FrmGetObjectPtr(
        formP, FrmGetObjectIndex(formP, A1cFormDateButtonId));
    Char *when = DpStateGet()->labelText;

    DpA1cFormatDate(DpStateGet()->a1cWhen, when, DP_LABEL_MAX);
    CtlSetLabel(ctlP, when);
    CtlDrawControl(ctlP);
}

static void A1cChangeDate(void) {
    DpState *st = DpStateGet();
    DateTimeType dt;
    Int16 month;
    Int16 day;
    Int16 year;

    TimSecondsToDateTime(st->a1cWhen, &dt);
    month = dt.month;
    day = dt.day;
    year = dt.year;

    if (!SelectDay(selectDayByDay, &month, &day, &year, "Lab A1c Date")) {
        return;
    }

    dt.month = (Int16)month;
    dt.day = (Int16)day;
    dt.year = (Int16)year;
    st->a1cWhen = TimDateTimeToSeconds(&dt);

    A1cRefreshDateButton();
}

void DpA1cOpen(void) {
    DpState *st = DpStateGet();

    /* A new lab value defaults to today: a result is normally entered the day
       it comes back. */
    st->a1cWhen = TimGetSeconds();

    DpA1cLabOpen();
    FrmGotoForm(A1cFormId);
}

Boolean DpA1cFormHandleEvent(EventPtr eventP) {
    switch (eventP->eType) {
        case frmOpenEvent:
            FrmDrawForm(FrmGetActiveForm());
            A1cRefreshDateButton();
            A1cDrawSummary();
            A1cRefresh(FrmGetActiveForm());
            return true;

        case ctlSelectEvent:
            switch (eventP->data.ctlSelect.controlID) {
                case A1cFormAddButtonId:
                    A1cAddValue();
                    return true;

                case A1cFormDeleteButtonId:
                    A1cDeleteValue();
                    return true;

                case A1cFormDateButtonId:
                    A1cChangeDate();
                    return true;

                case A1cFormDoneButtonId:
                    FrmGotoForm(ListFormId);
                    return true;

                default:
                    break;
            }
            break;

        case frmCloseEvent:
            A1cFreeRows();
            FldFreeMemory(A1cField(FrmGetActiveForm()));
            break;

        default:
            break;
    }

    return false;
}
