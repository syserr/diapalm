#include "dpentry.h"
#include "diapalm.h"
#include "resource.h"

/* Compile-time guard: the record layout must stay exactly what we serialize. */
typedef char DpEntrySizeCheck[(sizeof(DiapalmEntry) == 132) ? 1 : -1];

/***********************************************************************
 * Sorting
 ***********************************************************************/

static Int16 DpCompareEntries(void *rec1P, void *rec2P, Int16 other,
                              SortRecordInfoPtr rec1InfoP,
                              SortRecordInfoPtr rec2InfoP,
                              MemHandle appInfoH) {
    const DiapalmEntry *a = (const DiapalmEntry *)rec1P;
    const DiapalmEntry *b = (const DiapalmEntry *)rec2P;

    (void)other;
    (void)rec1InfoP;
    (void)rec2InfoP;
    (void)appInfoH;

    if (a->timestamp < b->timestamp) return -1;
    if (a->timestamp > b->timestamp) return 1;
    return 0;
}

/* Copies a stored record into the caller's struct, clamping the note to its
   recorded length so trailing garbage never reaches the UI. */
static void DpCopyOut(const DiapalmEntry *recP, DiapalmEntry *outP) {
    UInt16 len = recP->noteLen;

    if (len >= DP_NOTE_MAX) len = DP_NOTE_MAX - 1;

    outP->timestamp = recP->timestamp;
    outP->bloodSugarMgDl = recP->bloodSugarMgDl;
    outP->insulinTenths = recP->insulinTenths;
    outP->mealCarbsG = recP->mealCarbsG;
    MemMove(outP->note, recP->note, len);
    outP->note[len] = '\0';
    outP->noteLen = len;
}

/* Normalizes the note length before a record is written. */
static void DpNormalize(DiapalmEntry *entryP) {
    UInt16 len = StrLen(entryP->note);

    if (len >= DP_NOTE_MAX) len = DP_NOTE_MAX - 1;
    entryP->note[len] = '\0';
    entryP->noteLen = len;
}

/***********************************************************************
 * Database lifecycle
 ***********************************************************************/

/* True when the database is ours: a name match alone is not enough, since a
   same-named database of another type would be corrupted by our writes. */
static Boolean DpIsOurDatabase(UInt16 cardNo, LocalID dbID) {
    UInt32 type = 0;
    UInt32 creator = 0;

    if (DmDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, &type, &creator) != errNone) {
        return false;
    }

    return type == DP_DB_TYPE && creator == DP_DB_CREATOR;
}

DmOpenRef DpDbOpen(void) {
    DpState *st = DpStateGet();
    Err err;
    UInt16 cardNo = 0;
    LocalID dbID;

    if (st->db != NULL) return st->db;

    dbID = DmFindDatabase(cardNo, DP_DB_NAME);

    if (dbID != 0 && !DpIsOurDatabase(cardNo, dbID)) {
        /* Something else owns the name; leave it alone and make our own. */
        dbID = 0;
    }

    if (dbID == 0) {
        err = DmCreateDatabase(cardNo, DP_DB_NAME, DP_DB_CREATOR, DP_DB_TYPE,
                               false);
        if (err != errNone) return NULL;

        dbID = DmFindDatabase(cardNo, DP_DB_NAME);
        if (dbID == 0) return NULL;
    }

    st->dbId = dbID;
    st->db = DmOpenDatabase(cardNo, dbID, dmModeReadWrite);
    if (st->db == NULL) {
        st->dbId = 0;
        return NULL;
    }

    /* Older records sort ahead of newer ones so display can walk backwards. */
    err = DmInsertionSort(st->db, DpCompareEntries, 0);
    (void)err;

    return st->db;
}

void DpDbClose(void) {
    DpState *st = DpStateGet();

    if (st->db != NULL) {
        DmCloseDatabase(st->db);
        st->db = NULL;
    }
    st->dbId = 0;
}

DmOpenRef DpDb(void) { return DpStateGet()->db; }

LocalID DpDbLocalID(void) { return DpStateGet()->dbId; }

UInt16 DpCount(void) {
    DmOpenRef db = DpStateGet()->db;

    return (db == NULL) ? 0 : DmNumRecords(db);
}

/***********************************************************************
 * Record access
 ***********************************************************************/

Boolean DpGet(UInt16 index, DiapalmEntry *entryP) {
    DmOpenRef db = DpStateGet()->db;
    MemHandle h;
    DiapalmEntry *recP;

    if (db == NULL || index >= DmNumRecords(db)) return false;

    h = DmQueryRecord(db, index);
    if (h == NULL) return false;

    recP = (DiapalmEntry *)MemHandleLock(h);
    if (recP == NULL) return false;

    DpCopyOut(recP, entryP);

    MemHandleUnlock(h);
    return true;
}

UInt16 DpAdd(const DiapalmEntry *entryP) {
    DpState *st = DpStateGet();
    MemHandle h;
    DiapalmEntry *recP;
    DiapalmEntry entry;
    UInt16 index;
    Err err;

    if (st->db == NULL) return dmMaxRecordIndex;

    entry = *entryP;
    DpNormalize(&entry);

    index = DmFindSortPosition(st->db, &entry, NULL, DpCompareEntries, 0);

    h = DmNewRecord(st->db, &index, sizeof(DiapalmEntry));
    if (h == NULL) return dmMaxRecordIndex;

    recP = (DiapalmEntry *)MemHandleLock(h);
    if (recP == NULL) {
        DmRemoveRecord(st->db, index);
        return dmMaxRecordIndex;
    }

    /* Record data must be written through DmWrite: the memory manager may
       move or relocate the locked chunk, so a raw store through the pointer
       is not safe for database records. */
    err = DmWrite(recP, 0, &entry, sizeof(DiapalmEntry));
    MemHandleUnlock(h);
    DmReleaseRecord(st->db, index, true);

    if (err != errNone) {
        DmRemoveRecord(st->db, index);
        return dmMaxRecordIndex;
    }

    return index;
}

Boolean DpReplace(UInt16 index, const DiapalmEntry *entryP) {
    DpState *st = DpStateGet();
    MemHandle h;
    DiapalmEntry *recP;
    DiapalmEntry entry;
    UInt32 oldTimestamp;
    UInt16 newIndex;
    Err err;

    if (st->db == NULL || index >= DmNumRecords(st->db)) return false;

    entry = *entryP;

    h = DmGetRecord(st->db, index);
    if (h == NULL) return false;

    recP = (DiapalmEntry *)MemHandleLock(h);
    if (recP == NULL) {
        DmReleaseRecord(st->db, index, false);
        return false;
    }

    oldTimestamp = recP->timestamp;
    DpNormalize(&entry);
    err = DmWrite(recP, 0, &entry, sizeof(DiapalmEntry));
    MemHandleUnlock(h);
    DmReleaseRecord(st->db, index, true);
    if (err != errNone) return false;

    /* Only the timestamp drives the sort order, so that is the only case
       where the record has to move. */
    if (oldTimestamp != entry.timestamp) {
        newIndex = DmFindSortPosition(st->db, &entry, NULL, DpCompareEntries, 0);
        if (newIndex > index) newIndex--;
        if (newIndex != index) DmMoveRecord(st->db, index, newIndex);
    }

    return true;
}

Boolean DpDelete(UInt16 index) {
    DmOpenRef db = DpStateGet()->db;

    if (db == NULL || index >= DmNumRecords(db)) return false;
    return DmRemoveRecord(db, index) == errNone;
}

/***********************************************************************
 * Formatting helpers
 ***********************************************************************/

void DpFormatWhen(UInt32 timestamp, Char *outP, UInt16 outSize) {
    DateTimeType dt;
    Char time[timeStringLength];

    TimSecondsToDateTime(timestamp, &dt);

    if (outSize < 20) {
        /* No room for the year: the day matters more than which year. */
        StrPrintF(outP, "%d/%d %d:%02d%s", dt.month, dt.day,
                  (dt.hour % 12) == 0 ? 12 : (dt.hour % 12), dt.minute,
                  dt.hour < 12 ? "a" : "p");
        return;
    }

    /* The button label is about 84 pixels, so keep this terse; the year is
       only informative here, the picker next to it shows it in full. */
    TimeToAscii(dt.hour, dt.minute,
                (TimeFormatType)PrefGetPreference(prefTimeFormat), time);
    StrPrintF(outP, "%d/%d/%d %s", dt.month, dt.day, (UInt16)dt.year, time);
}

void DpFormatShortWhen(UInt32 timestamp, Char *outP, UInt16 outSize) {
    DateTimeType dt;

    TimSecondsToDateTime(timestamp, &dt);

    /* 10/17 8:03p — deliberately terse so a row fits in 160 pixels. */
    StrPrintF(outP, "%d/%d %d:%02d%s", dt.month, dt.day,
              (dt.hour % 12) == 0 ? 12 : (dt.hour % 12), dt.minute,
              dt.hour < 12 ? "a" : "p");
    (void)outSize;
}

void DpFormatInsulin(UInt16 tenths, Char *outP, UInt16 outSize) {
    if (outSize == 0) return;

    if (tenths == 0) {
        outP[0] = '\0';
        return;
    }

    if ((tenths % 10) == 0) {
        StrPrintF(outP, "%d", tenths / 10);
    } else {
        StrPrintF(outP, "%d.%d", tenths / 10, tenths % 10);
    }
}

UInt16 DpParseUInt(const Char *textP) {
    Int32 value;

    if (textP == NULL) return 0;

    while (*textP == ' ' || *textP == '\t') textP++;
    if (*textP == '\0') return 0;

    value = StrAToI(textP);
    if (value <= 0) return 0;
    if (value > 65535) return 65535;

    return (UInt16)value;
}

void DpFormatUInt(UInt16 value, Char *outP, UInt16 outSize) {
    if (outSize == 0) return;

    if (value == 0) {
        outP[0] = '\0';
        return;
    }

    StrIToA(outP, (Int32)value);
}

/* Parses "2.5" or "2" into tenths, saturating at 65535. */
UInt16 DpParseTenths(const Char *textP) {
    UInt32 whole = 0;
    UInt32 frac = 0;
    UInt32 result;

    if (textP == NULL) return 0;

    while (*textP == ' ' || *textP == '\t') textP++;

    while (*textP >= '0' && *textP <= '9') {
        whole = whole * 10 + (UInt32)(*textP - '0');
        if (whole > 6553) return 65535;
        textP++;
    }

    if (*textP == '.') {
        textP++;
        if (*textP >= '0' && *textP <= '9') {
            frac = (UInt32)(*textP - '0');
        }
    }

    result = whole * 10 + frac;
    if (result > 65535) result = 65535;

    return (UInt16)result;
}
