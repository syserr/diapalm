#include "dplist.h"
#include "diapalm.h"
#include "dpentry.h"
#include "dpedit.h"
#include "dpa1c.h"
#include "resource.h"

/* The list shows every entry newest-first. Row i in the list is storage
   record (count - 1 - i), because storage is sorted oldest-first so that
   DmFindSortPosition inserts backdated entries correctly. */

static DmOpenRef ListDb(void) { return DpDb(); }

static UInt16 ListCount(void) {
    DmOpenRef db = ListDb();

    return (db == NULL) ? 0 : DmNumRecords(db);
}

static Int16 ListStorageIndex(Int16 itemNum) {
    return (Int16)(ListCount() - 1 - (UInt16)itemNum);
}

/* Builds the right-hand summary: "BG 123  I 2.5  C 40", or the first line of
   the note when no readings were recorded. */
static void ListRowText(UInt16 storageIndex, Char *outP, UInt16 outSize) {
    DiapalmEntry entry;
    Char num[8];
    Char *p = outP;
    UInt16 left;
    Boolean any = false;

    outP[0] = '\0';

    if (!DpGet(storageIndex, &entry)) return;

    if (entry.bloodSugarMgDl != 0) {
        p += StrLen(StrCopy(p, "BG "));
        DpFormatUInt(entry.bloodSugarMgDl, num, sizeof(num));
        p += StrLen(StrCopy(p, num));
        any = true;
    }

    if (entry.insulinTenths != 0) {
        if (any) p += StrLen(StrCopy(p, "  "));
        p += StrLen(StrCopy(p, "I "));
        DpFormatInsulin(entry.insulinTenths, num, sizeof(num));
        p += StrLen(StrCopy(p, num));
        any = true;
    }

    if (entry.mealCarbsG != 0) {
        if (any) p += StrLen(StrCopy(p, "  "));
        p += StrLen(StrCopy(p, "C "));
        DpFormatUInt(entry.mealCarbsG, num, sizeof(num));
        p += StrLen(StrCopy(p, num));
        any = true;
    }

    if (!any) {
        (void)outSize;
        StrNCopy(outP, entry.note, (Int16)outSize - 1);
        left = StrLen(outP);
        /* A note may contain line breaks; show only the first one. */
        while (left > 0) {
            left--;
            if (outP[left] == '\n' || outP[left] == '\r' ||
                outP[left] == 0x0A) {
                outP[left] = '\0';
            }
        }
    }
}

/* Builds one row as a single string: "M/D HH:MM  BG 123  I 2.5  C 40". */
#define DP_ROW_MAX 64

static void ListBuildRow(UInt16 storageIndex, Char *outP, UInt16 outSize) {
    DiapalmEntry entry;
    Char when[16];

    outP[0] = '\0';

    if (!DpGet(storageIndex, &entry)) return;

    DpFormatShortWhen(entry.timestamp, when, sizeof(when));
    StrNCopy(outP, when, (Int16)outSize - 1);
    outP[outSize - 1] = '\0';

    {
        Char text[48];
        UInt16 used = StrLen(outP);

        ListRowText(storageIndex, text, sizeof(text));
        if (text[0] == '\0' || used + 2 >= outSize) return;

        outP[used] = ' ';
        outP[used + 1] = ' ';
        outP[used + 2] = '\0';
        StrNCopy(outP + used + 2, text, (Int16)(outSize - used - 3));
        outP[outSize - 1] = '\0';
    }
}

/* Frees the row-string chunk handed to the list. The list must not be left
   pointing at freed memory, so the caller clears the choices first. */
static void ListFreeRows(void) {
    DpState *st = DpStateGet();

    if (st->listRowsH != NULL) {
        MemHandleUnlock(st->listRowsH);
        MemHandleFree(st->listRowsH);
        st->listRowsH = NULL;
    }
}

static void ListRefresh(void) {
    FormType *formP = FrmGetActiveForm();
    ListType *listP =
        (ListType *)FrmGetObjectPtr(formP, FrmGetObjectIndex(formP,
                                                             ListFormListId));
    DpState *st = DpStateGet();
    UInt16 count = ListCount();

    LstSetListChoices(listP, NULL, 0);
    ListFreeRows();

    if (count == 0) {
        LstSetSelection(listP, noListSelection);
        LstDrawList(listP);
        return;
    }

    /* One chunk holds the pointer array followed by the row text, so the list
       has a single block to point into. It stays locked while the form lives:
       the list keeps the pointer, and unlocking could move the block. */
    {
        UInt16 size = (UInt16)(count * (sizeof(Char *) + DP_ROW_MAX));
        MemHandle h = MemHandleNew(size);

        if (h != NULL) {
            Char **items = (Char **)MemHandleLock(h);
            Char *pool = (Char *)(items + count);
            UInt16 i;

            for (i = 0; i < count; i++) {
                items[i] = pool;
                ListBuildRow((UInt16)ListStorageIndex((Int16)i), pool,
                             DP_ROW_MAX);
                pool += DP_ROW_MAX;
            }

            st->listRowsH = h;
            LstSetListChoices(listP, items, (Int16)count);

            /* LstSetListChoices drops the selection when the count changes,
               and the Entry menu acts on the selection, so put one back. */
            LstSetSelection(listP, 0);
        }
    }

    LstDrawList(listP);
}

/* An empty list is a blank box, which reads as a broken form rather than an
   empty diary, so say so. Two short strings rather than one long one: the
   160px screen has no room for a wrapped sentence. */
/* WinDrawChars paints glyphs and nothing else, so a centred string shorter
   than the one it replaces leaves the old one's tail behind. Every line the
   list form draws on the form itself is cleared first. */
static void ListEraseBand(Int16 y, Int16 height) {
    RectangleType r;
    PatternType saved = WinGetPatternType();

    r.topLeft.x = 0;
    r.topLeft.y = y;
    r.extent.x = 160;
    r.extent.y = height;

    WinSetPatternType(whitePattern);
    WinEraseRectangle(&r, 0);
    WinSetPatternType(saved);
}

static void ListDrawHintLine(UInt16 stringId, Int16 y) {
    MemHandle h = DmGet1Resource(strRsc, stringId);
    const Char *textP;
    Int16 width;

    if (h == NULL) return;

    textP = (const Char *)MemHandleLock(h);
    if (textP != NULL) {
        width = FntCharsWidth(textP, StrLen(textP));
        ListEraseBand(y, FntLineHeight());
        WinDrawChars(textP, StrLen(textP),
                     (160 - width) / 2, y);
        MemHandleUnlock(h);
    }
}

static void ListDrawEmptyHint(void) {
    RectangleType bounds;
    Int16 lineHeight;
    Int16 y;

    if (ListCount() != 0) return;

    FrmGetObjectBounds(FrmGetActiveForm(),
                       FrmGetObjectIndex(FrmGetActiveForm(), ListFormListId),
                       &bounds);

    lineHeight = FntLineHeight();
    y = bounds.topLeft.y + (bounds.extent.y - 2 * lineHeight - 4) / 2;

    ListDrawHintLine(EmptyListStrId, y);
    ListDrawHintLine(EmptyListHintStrId, y + lineHeight + 4);
}

/* Where the A1c line is drawn, kept in one place because the tap handler has
   to hit the same band the text occupies. */
static Int16 ListA1cY(void) {
    RectangleType bounds;

    FrmGetObjectBounds(FrmGetActiveForm(),
                       FrmGetObjectIndex(FrmGetActiveForm(), ListFormListId),
                       &bounds);

    return bounds.topLeft.y + bounds.extent.y + 3;
}

/* The A1c line sits under the list, on the form rather than in it: the list
   is a scrolling object and a value there would scroll away. Drawn after
   LstDrawList for the same reason the empty-list hint is. */
static void ListDrawA1c(void) {
    Char text[32];
    Int16 width;
    Int16 y = ListA1cY();

    /* Cleared before the early return as well: the line changes width between
       states ("est. A1c 7.2%" and "A1c 7.4%  lab 10/18/2026"), so a state with
       nothing to say still has to wipe the previous one. */
    ListEraseBand(y, FntLineHeight());

    DpA1cStatusText(text, sizeof(text));
    if (text[0] == '\0') return;

    /* Centred like the empty-list hint. The longest string the module emits
       is "est. A1c --  no readings", which measures about 116px, so this
       stays on screen; the clamp is only a guard against a future string. */
    width = FntCharsWidth(text, StrLen(text));
    if (width > 158) {
        WinDrawChars(text, StrLen(text), 1, y);
        return;
    }

    WinDrawChars(text, StrLen(text), (160 - width) / 2, y);
}

/* The status line is the one place the A1c value is visible without opening a
   menu, so tapping it opens the A1c form. A tap that misses the line falls
   through to the list, which is what a user aiming at a row expects. */
static Boolean ListA1cTap(EventPtr eventP) {
    Int16 y = ListA1cY();
    Int16 lineHeight = FntLineHeight();
    Int16 tapY = eventP->screenY;

    if (tapY < y - 1 || tapY > y + lineHeight) return false;

    DpA1cOpen();
    return true;
}

UInt16 DpListSelectedIndex(void) {
    FormType *formP = FrmGetActiveForm();
    ListType *listP;
    Int16 itemNum;

    if (formP == NULL || FrmGetActiveFormID() != ListFormId) return DP_NEW_ENTRY;

    listP = (ListType *)FrmGetObjectPtr(
        formP, FrmGetObjectIndex(formP, ListFormListId));
    itemNum = LstGetSelection(listP);

    if (itemNum < 0) return DP_NEW_ENTRY;

    return (UInt16)ListStorageIndex(itemNum);
}

static void ListScroll(Boolean down) {
    FormType *formP = FrmGetActiveForm();
    ListType *listP =
        (ListType *)FrmGetObjectPtr(formP, FrmGetObjectIndex(formP,
                                                             ListFormListId));

    (void)LstScrollList(listP, down ? winDown : winUp, 1);
}

Boolean DpListFormHandleEvent(EventPtr eventP) {
    switch (eventP->eType) {
        case frmOpenEvent:
            DpSyncMenu();
            FrmDrawForm(FrmGetActiveForm());
            ListRefresh();
            /* After LstDrawList, which paints over the list's own bounds. */
            ListDrawEmptyHint();
            ListDrawA1c();
            return true;

        case frmCloseEvent:
            ListFreeRows();
            return false;

        case lstSelectEvent:
            if (eventP->data.lstSelect.listID == ListFormListId) {
                FormType *formP = FrmGetActiveForm();
                ListType *listP = (ListType *)FrmGetObjectPtr(
                    formP, FrmGetObjectIndex(formP, ListFormListId));
                Int16 itemNum = LstGetSelection(listP);

                if (itemNum >= 0) {
                    DpEditOpen((UInt16)ListStorageIndex(itemNum));
                }
                return true;
            }
            break;

        case penDownEvent:
            if (ListA1cTap(eventP)) return true;
            break;

        case keyDownEvent:
            if (eventP->data.keyDown.chr == chrPageUp ||
                eventP->data.keyDown.chr == chrUpArrow) {
                ListScroll(false);
                return true;
            }
            if (eventP->data.keyDown.chr == chrPageDown ||
                eventP->data.keyDown.chr == chrDownArrow) {
                ListScroll(true);
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}
