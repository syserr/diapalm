#include "dpexport.h"
#include "diapalm.h"
#include "dpentry.h"

/* One CSV row is at most 40 characters of fixed fields plus a quoted note of
   at most 2 * (DP_NOTE_MAX - 1) + 2 characters. */
#define DP_CSV_MAX (64 + 2 * DP_NOTE_MAX)

static Char *Put2(Char *p, UInt16 value) {
    *p++ = (Char)('0' + (value / 10) % 10);
    *p++ = (Char)('0' + value % 10);
    return p;
}

static Char *Put4(Char *p, UInt16 value) {
    *p++ = (Char)('0' + (value / 1000) % 10);
    *p++ = (Char)('0' + (value / 100) % 10);
    *p++ = (Char)('0' + (value / 10) % 10);
    *p++ = (Char)('0' + value % 10);
    return p;
}

/* Appends the note as a quoted CSV field, doubling embedded quotes. */
static Char *PutQuotedNote(Char *p, const Char *noteP) {
    *p++ = '"';
    while (*noteP != '\0') {
        if (*noteP == '"') *p++ = '"';
        *p++ = *noteP++;
    }
    *p++ = '"';
    return p;
}

static UInt16 BuildRow(const DiapalmEntry *entryP, Char *outP) {
    DateTimeType dt;
    Char *p = outP;

    TimSecondsToDateTime(entryP->timestamp, &dt);

    p = Put4(p, (UInt16)dt.year);
    *p++ = '-';
    p = Put2(p, (UInt16)dt.month);
    *p++ = '-';
    p = Put2(p, (UInt16)dt.day);
    *p++ = 'T';
    p = Put2(p, (UInt16)dt.hour);
    *p++ = ':';
    p = Put2(p, (UInt16)dt.minute);
    *p++ = ':';
    p = Put2(p, (UInt16)dt.second);
    *p++ = ',';

    if (entryP->bloodSugarMgDl != 0) {
        p += StrLen(StrIToA(p, (Int32)entryP->bloodSugarMgDl));
    }
    *p++ = ',';

    if (entryP->insulinTenths != 0) {
        p += StrLen(StrIToA(p, (Int32)(entryP->insulinTenths / 10)));
        if ((entryP->insulinTenths % 10) != 0) {
            *p++ = '.';
            *p++ = (Char)('0' + entryP->insulinTenths % 10);
        }
    }
    *p++ = ',';

    if (entryP->mealCarbsG != 0) {
        p += StrLen(StrIToA(p, (Int32)entryP->mealCarbsG));
    }
    *p++ = ',';

    p = PutQuotedNote(p, entryP->note);

    return (UInt16)(p - outP);
}

static Boolean AppendRecord(DmOpenRef db, UInt16 index, const Char *textP,
                            UInt16 len) {
    MemHandle h;
    Char *recP;

    h = DmNewRecord(db, &index, (UInt32)len);
    if (h == NULL) return false;

    recP = (Char *)MemHandleLock(h);
    if (recP == NULL) {
        DmRemoveRecord(db, index);
        return false;
    }

    DmWrite(recP, 0, textP, len);
    MemHandleUnlock(h);
    DmReleaseRecord(db, index, true);

    return true;
}

UInt16 DpExportCsv(void) {
    UInt16 cardNo = 0;
    LocalID dbId;
    DmOpenRef db;
    DiapalmEntry entry;
    Char row[DP_CSV_MAX];
    UInt16 index = 0;
    UInt16 count = DpCount();
    UInt16 i;
    UInt16 len;

    dbId = DmFindDatabase(cardNo, DP_EXPORT_NAME);
    if (dbId != 0) {
        if (DmDeleteDatabase(cardNo, dbId) != errNone) return dmMaxRecordIndex;
    }

    if (DmCreateDatabase(cardNo, DP_EXPORT_NAME, DP_EXPORT_CREATOR,
                         DP_EXPORT_TYPE, false) != errNone) {
        return dmMaxRecordIndex;
    }

    dbId = DmFindDatabase(cardNo, DP_EXPORT_NAME);
    if (dbId == 0) return dmMaxRecordIndex;

    db = DmOpenDatabase(cardNo, dbId, dmModeReadWrite);
    if (db == NULL) return dmMaxRecordIndex;

    len = StrLen("datetime_iso,glucose_mgdl,insulin_units,carbs_g,note");
    if (!AppendRecord(db, index++, "datetime_iso,glucose_mgdl,insulin_units,"
                                  "carbs_g,note", len)) {
        DmCloseDatabase(db);
        return dmMaxRecordIndex;
    }

    for (i = 0; i < count; i++) {
        if (!DpGet(i, &entry)) continue;

        len = BuildRow(&entry, row);
        if (!AppendRecord(db, index++, row, len)) {
            DmCloseDatabase(db);
            return dmMaxRecordIndex;
        }
    }

    DmCloseDatabase(db);

    /* Header is record 0, so the number of data rows is index - 1. */
    return (UInt16)(index - 1);
}
