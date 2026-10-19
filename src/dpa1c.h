/* A1c: the estimated value derived from the last 90 days of blood sugar
   readings, and the lab values the user types in by hand.

   The estimate is the formula the Android app uses (shared/Helper.java):
   percent = 0.031 * avgMgDl + 2.393. Everything here is integer math -- the
   68K has no FPU, and soft-float would drag a library into a 9 KB image.

   Lab values live in their own database rather than on DiapalmEntry: that
   record is pinned at 132 bytes by a compile-time guard, and a lab A1c is a
   dated series worth keeping, not one more field on a log entry. The Android
   app stores every lab value too, but only ever reads the newest one, so its
   history is invisible; here it is listed. */

#ifndef DIAPALM_A1C_H
#define DIAPALM_A1C_H

#include <PalmOS.h>

/* A1c values are carried as tenths of a percent, so 7.2% is 72. Zero means
   "nothing to show" throughout this module. */

/* Lab values get their own database: they are a dated series, not a field on a
   log entry, and the entry record is pinned at 132 bytes by a compile-time
   guard. The name must not collide with the application's own database, which
   shares the creator and is called "Diapalm". */
#define DP_A1C_DB_NAME "Diapalm A1c"
#define DP_A1C_DB_TYPE 'a1cl'

/* Widest row the A1c list builds: "10/12/26    6.8%". */
#define DP_A1C_ROW_MAX 32

/* The averaging window, in days. The Android app uses DateTime.minusMonths(3);
   90 days is the same physiological window without month-arithmetic edge
   cases, which this platform has no helper for. */
#define DP_A1C_WINDOW_DAYS 90

/* How long a lab value speaks for the current reading. The Android app shows
   a hand-entered HbA1c in place of the estimate for a month and then falls
   back to the estimate, so a stale lab result cannot mask today's average. */
#define DP_A1C_LAB_DAYS 30

/* Accepted range for a hand-entered lab value, in tenths. Wide enough for a
   real result at either extreme, narrow enough that "68" typed for 6.8% is
   caught rather than stored as 68%. */
#define DP_A1C_MIN_TENTHS 20  /* 2.0% */
#define DP_A1C_MAX_TENTHS 250 /* 25.0% */

/***********************************************************************
 * The estimate
 ***********************************************************************/

/* Estimated A1c over the last 90 days, in tenths of a percent, or 0 when no
   blood sugar reading falls inside that window. Non-NULL out-params receive
   the average mg/dL and the number of readings the estimate came from. */
UInt16 DpA1cEstimate(UInt16 *avgMgDlP, UInt16 *readingCountP);

/***********************************************************************
 * Lab values
 ***********************************************************************/

/* Opens (creating if needed) the lab-value database. Returns NULL if it could
   not be opened, in which case the estimate still works and the history is
   simply empty. */
DmOpenRef DpA1cLabOpen(void);

/* Closes the lab-value database. Safe when nothing is open. */
void DpA1cLabClose(void);

UInt16 DpA1cLabCount(void);

/* Reads the lab value at index (storage order: oldest first). Either out-param
   may be NULL. Returns false when the index is out of range. */
Boolean DpA1cLabGet(UInt16 index, UInt32 *whenP, UInt16 *tenthsP);

/* Inserts a dated lab value in sorted position and returns its index, or
   dmMaxRecordIndex on failure. */
UInt16 DpA1cLabAdd(UInt32 when, UInt16 tenths);

/* Deletes the lab value at index. */
Boolean DpA1cLabDelete(UInt16 index);

/* The newest lab value dated at or before now, or 0 when there is none. */
UInt16 DpA1cLabLatest(UInt32 *whenP);

/* The value the status line and the A1c form should show. A lab value younger
   than DP_A1C_LAB_DAYS wins; otherwise the estimate; otherwise, when there is
   no estimate at all, the newest lab value of any age -- a number from three
   months ago still beats telling the user there is nothing. Returns tenths,
   or 0 when there is genuinely nothing to show. *isLabP says which source it
   came from and *whenP receives that lab value's date. */
UInt16 DpA1cCurrent(Boolean *isLabP, UInt32 *whenP);

/***********************************************************************
 * Formatting
 ***********************************************************************/

/* Formats tenths as "7.2"; an empty string when 0. */
void DpA1cFormat(UInt16 tenths, Char *outP, UInt16 outSize);

/* Formats a timestamp as "10/12/26" -- the date alone. A lab value is a dated
   blood draw, so its time of day is meaningless. */
void DpA1cFormatDate(UInt32 timestamp, Char *outP, UInt16 outSize);

/* Parses "6.8" or "6" into tenths. Returns false when the text is not a
   number inside DP_A1C_MIN_TENTHS..DP_A1C_MAX_TENTHS, which is how a missing
   decimal point gets caught. *tenthsP is left at 0 on failure. */
Boolean DpA1cParsePercent(const Char *textP, UInt16 *tenthsP);

/* The one-line summary drawn under the list. */
void DpA1cStatusText(Char *outP, UInt16 outSize);

/***********************************************************************
 * The A1c form
 ***********************************************************************/

/* Opens the A1c form: the estimate, the lab-value history, and the field for
   adding one. The date for a new value starts at today. */
void DpA1cOpen(void);

/* Event handler for the A1c form. */
Boolean DpA1cFormHandleEvent(EventPtr eventP);

#endif /* DIAPALM_A1C_H */
