/* Entry storage: one fixed-size record per log entry in the "Diapalm Entries"
   PDB.

   The name must not collide with the application's own database, which shares
   the creator and is called "Diapalm": opening that one and sorting it would
   corrupt the app's resource list. */

#ifndef DIAPALM_ENTRY_H
#define DIAPALM_ENTRY_H

#include <PalmOS.h>
#include "diapalm.h"

#define DP_DB_NAME "Diapalm Entries"
#define DP_DB_TYPE 'diap'
#define DP_DB_CREATOR 'dprd'

#define DP_NOTE_MAX 120

/* Numeric readings are 0 when not recorded. Insulin is stored in tenths of a
   unit so the record stays integer-only. */
typedef struct {
    UInt32 timestamp;      /* seconds since 1904-01-01, device local time */
    UInt16 bloodSugarMgDl;
    UInt16 insulinTenths;
    UInt16 mealCarbsG;
    UInt16 noteLen;
    Char note[DP_NOTE_MAX];
} DiapalmEntry;

/* Opens (creating if needed) the Diapalm database and makes sure it is sorted
   by timestamp. Returns NULL if the database could not be opened. */
DmOpenRef DpDbOpen(void);

/* Closes the database. Safe to call when nothing is open. */
void DpDbClose(void);

/* The open database, or NULL. */
DmOpenRef DpDb(void);

/* Local database id of the Diapalm database, or 0. Used for alarms. */
LocalID DpDbLocalID(void);

UInt16 DpCount(void);

/* Reads the entry at index into *entryP. Index is in storage (timestamp
   ascending) order. Returns false if the index is out of range. */
Boolean DpGet(UInt16 index, DiapalmEntry *entryP);

/* Inserts a new entry in sorted position and returns its index, or
   dmMaxRecordIndex on failure. */
UInt16 DpAdd(const DiapalmEntry *entryP);

/* Overwrites the entry at index, re-sorting if its timestamp moved. Returns
   false on failure. */
Boolean DpReplace(UInt16 index, const DiapalmEntry *entryP);

/* Deletes the entry at index. */
Boolean DpDelete(UInt16 index);

/* Formats the stored timestamp as "10/17/26 8:03 pm" (max ~24 chars). */
void DpFormatWhen(UInt32 timestamp, Char *outP, UInt16 outSize);

/* Formats the stored timestamp as "10/17 8:03p" for list rows. */
void DpFormatShortWhen(UInt32 timestamp, Char *outP, UInt16 outSize);

/* Formats insulin tenths as "2.5" (or "2" when whole; "" when 0). */
void DpFormatInsulin(UInt16 tenths, Char *outP, UInt16 outSize);

/* Parses a field's decimal text. Blank or unparsable yields 0. */
UInt16 DpParseUInt(const Char *textP);

/* Parses "2.5"/"2" into tenths. Blank or unparsable yields 0. */
UInt16 DpParseTenths(const Char *textP);

/* Formats 0 as "" and anything else as decimal digits. */
void DpFormatUInt(UInt16 value, Char *outP, UInt16 outSize);

#endif /* DIAPALM_ENTRY_H */
