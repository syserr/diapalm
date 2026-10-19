# Original plan: Diapalm for Palm OS 68K (core MVP)

> **Historical.** Written before any code existed, so file names and layout below (a `palm-app/` subdirectory, `entry.h`, `list.h`, ...) differ from the finished project. See `README.md` for the current layout and `DEBRIEF.md` for what changed.

> Saved 2026-10-17 from the approved Claude Code plan.

## Context

A friend of the user wants to run Diaguard (this repo — an Android diabetes diary) on a classic Palm OS device. We agreed on **classic 68K** target and a **core MVP** scope: log entries (blood sugar, insulin units, meal carbs, note, timestamp), entry list, PDB storage, CSV export, and one "log glucose" reminder. The app will live in a new `palm-app/` subdirectory, built on macOS with [palmsnipe/palm-toolchain](https://github.com/palmsnipe/palm-toolchain) and tested in **CloudpilotEmu** (POSE is Windows-only).

Domain model (from the Android app): an Entry is timestamp + note + measurements. For the MVP we store **one fixed-size record per entry** carrying all values — no heap, zero parsing in draw callbacks, 1:1 with CSV rows.

**API corrections verified against live docs** (palm.wiki 6.0.1 reference + SDK headers): the alarm API is `AlmSetAlarm`/`AlmGetAlarm` (not `SysAddAlarm`); the list API is `Lst*` (not `List*`); database APIs are `Pdb*` (DB-level) + `Dm*` (record-level); Palm `DateTime` = seconds since 1904-01-01 in **device-local time** (no 30-min offset); alarm fires deliver launch codes 6 (`AlarmTriggered`) then 7 (`DisplayAlarm`, where we show UI).

## Phase 0 — Toolchain & SDK (de-risk first)

No toolchain or SDK exists on this machine yet (verified 2026-10-17).

1. `git clone https://github.com/palmsnipe/palm-toolchain.git` (into a local directory); run its setup (`brew bundle`, `make setup` with `PALM_SDK_SOURCE`).
2. Obtain genuine **Palm OS SDK 5r4**: [PalmDB SDK page](https://palmdb.net/app/palm-os-sdk) (`Palm_OS_SDK_Garnet_5r4.zip`, 26 MB) or [archive.org/palm-os-sdk](https://archive.org/download/palm-os-sdk). Verify the tree has `include/Core/System/PdbMgr.h` and `include/Core/UI/Dialog.h` (some GitHub mirrors are reduced and lack these — fallbacks noted below).
3. Build the toolchain's `examples/hello-world`, install + run it in CloudpilotEmu (68K ROM, e.g. Palm OS 3.5/4.0). **Nothing in `palm-app/` is written until hello-world runs in the emulator.**

## Phase 1 — App skeleton

Directory layout (final):

```
palm-app/
├── Makefile              # adapted verbatim from toolchain hello-world Makefile
├── README.md             # setup + build + run instructions
├── .gitignore            # build/
├── linker.lkr            # copied from toolchain example
├── src/
│   ├── startup.c         # copied from toolchain example
│   ├── diapalm.h        # DiapalmEntry struct, FourCCs, constants, module prototypes
│   ├── app.c             # PilotMain, launch-code dispatch, event loop, menu, form nav
│   ├── entry.h/.c        # PDB ensure/open, record CRUD, sort comparator, time helpers
│   ├── list.h/.c         # ListForm: fill/draw/selection/delete
│   ├── edit.h/.c         # New/Edit form: field load/save, validation, time change
│   ├── export.h/.c       # CSV export PDB
│   └── alarm.h/.c        # AlmSetAlarm/AlmGetAlarm wrappers, fire handling
├── resources/
│   ├── resource.h        # #define IDs shared by app.rcp and C
│   └── app.rcp           # menu bar, ListForm, EditForm, alert, version, strings
└── build/                # gitignored
```

**Record schema** (`diapalm.h`):
```c
typedef struct {
    UInt32 timestamp;      /* seconds since 1904-01-01, device local time */
    UInt16 bloodSugarMgDl; /* 0 = not recorded */
    UInt16 insulinTenths;  /* units × 10, 0 = not recorded */
    UInt16 mealCarbsG;     /* 0 = not recorded */
    UInt16 noteLen;
    Char   note[120];      /* always NUL-terminated */
} DiapalmEntry;           /* 132 bytes under -mshort; compile-time sizeof guard */
```
DB: name `"Diapalm"`, type `'diap'`, creator `'dprd'`. Open read-write once at launch, close at exit. Sort: `PdbMakeIndex` with comparator on `timestamp` (backdated entries sort correctly; O(log n) insert via `DmFindSortPosition`); display newest-first by iterating the index downward. Fallback if only reduced `Dm*`-named DB API is available: sort in memory in `DpListFill()` with the same comparator.

## Phase 2 — UI (160×160, client 160×142)

- **ListForm** (id 2000): `LIST` filling the client area. On open: `LstSetListChoices` + `LstSetDrawFunction` + `LstDrawList`. Row = `M/D/YY HH:MM` left, `BG 123  I 2.5  C 40` right (missing values omitted; note text if all missing). Open entry by tapping the item (`penDownEvent` inside list bounds) and/or Enter/Menu→Edit. Empty DB draws a hint string.
- **EditForm** (id 2010): note `FIELD` (multiline, maxchars 80), Time row with "Set" button, three right-aligned numeric fields (BG mg/dL, Insulin U, Carbs g), buttons **Cancel / Delete / OK** (Delete hidden in New mode via `FrmHideObject`). Validation on OK: BG 0–999, insulin 0.1–99.9, carbs 0–999, empty = not recorded; all-empty → `FrmCustomAlert` "Nothing to save." Save → create or update record → `FrmGotoForm(ListFormId)` (list refreshes on open).
- **Delete confirmation**: `FrmCustomAlert` with a custom `ALERT` resource (fallback: system `sysAlertDelete` id).
- **Menu bar** (id 1000): File → New Entry… / Export CSV… · Entry → Edit… / Delete · Remind → Set Reminder… / Cancel Reminder (hidden/show via `MenuHideItem` based on `AlmGetAlarm`) · Help → About.
- **Time picker**: `DlgShowDateTimeDlg` from the genuine SDK's `Dialog.h`; fallback is a small 4th form (hour/minute/AM-PM) if the header is missing.
- Scrolling: `EvrpGetScrollEvent` + `LstScrollList` on auto-repeat keyDown.

## Phase 3 — CSV export

Menu → File → Export CSV… builds a **`"Diapalm Export"` PDB** (type `'DIEX'`) from scratch each time (delete if exists → create → one record per line, record 0 = header). Chosen over Beaming/Exg because it works in the emulator and is readable by any PDB tool. Format (comma-separated, local time):

```
datetime_iso,glucose_mgdl,insulin_units,carbs_g,note
2026-10-17T08:30:00,123,2.5,40,"Ate pancakes"
```
Ascending timestamp order; missing numeric = empty; note always double-quoted with `"` doubled. Result: `FrmAlert` with entry count.

## Phase 4 — Reminder

One-shot `AlmSetAlarm(0, dbID, ref, alarmSeconds, false)` registered on the app DB; `ref` = our FourCC, echoed back in the param block. Set: pick a time (today, or tomorrow if past), confirm with `FrmAlert`. Cancel: `AlmSetAlarm(..., 0, false)`. On fire: `sysAppLaunchCmdDisplayAlarm` (7) → run UI and open the **new-entry form** pre-filled with now (sanctioned pattern — code 6 `AlarmTriggered` is only for rescheduling recurring alarms, which we don't need).

## Build system

Makefile mirrors the toolchain hello-world Makefile exactly: `m68k-none-elf-gcc` with `-m68000 -mno-align-int -mpcrel -fpic -fshort-enums -mshort -Os`, two-stage link + `generate-m68k-relocs.mjs` (required for 68K trap relocation), `objcopy -j .vec -j .text` → `code0001.bin`, `pilrc -ro` baking `DATA "code" ID 1` + resources, `build-prc --no-check` → `build/Diapalm.prc`, plus a `make test` target running the toolchain's `validate-prc.mjs`. Targets: `all`, `resources`, `clean`, `test`.

## Verification

1. hello-world builds and runs in CloudpilotEmu (Phase 0 gate).
2. `make` → `make test` → inspect `Diapalm.prc` (type `appl`, creator `dprd`, code + resources present).
3. Install in CloudpilotEmu, then exercise: new entry → top of list; backdated entry sorts below; edit updates; delete (both paths) with confirm; note-only entry row + CSV; export → `Diapalm Export.pdb` readable on the Mac, diff CSV byte-for-byte; re-export replaces; set reminder ~1 min out, advance emulator clock → alarm opens new-entry form; cancel reminder; reboot emulator → data persists.
4. Error paths: invalid BG on OK → alert, stays on form; all-empty save → alert; export with 0 entries → header-only CSV + count alert.

## Risks (checked during implementation, in order)

1. SDK variant (genuine 5r4 vs reduced mirror) → documented `Pdb*`→`Dm*` fallbacks; hello-world build is the canary.
2. `pilrc` 3.2 `ALERT` resource syntax → verify on first resource build; fallback `sysAlertDelete`.
3. `EvrpGetScrollEvent` missing in a reduced Event.h → soft-key scroll fallback.
4. `DlgShowDateTimeDlg` missing → 4th mini-form time picker.
5. Tap-to-open on list items → Menu/Enter always works; tap is an enhancement.
6. SDK download: if palmdb.net/archive.org both fail, ask the user for a local copy.

Known out-of-scope: 2031 year rollover (7-bit `DateType.year` — data stored as raw seconds survives, only display is affected).

## Key references

- Toolchain: [palmsnipe/palm-toolchain](https://github.com/palmsnipe/palm-toolchain) (Makefile/`main.c`/`app.rcp`/`linker.lkr` from `examples/hello-world` are the source of truth for build + event loop)
- API: [palm.wiki 6.0.1 reference](https://palm.wiki/development/docs/601/PalmOSReference/Reference_Front.html) — [AlarmManager](https://palm.wiki/development/docs/601/PalmOSReference/AlarmManager.html), [AppLaunchCodes](https://palm.wiki/development/docs/601/PalmOSReference/AppLaunchCodes.html), [Data & Resource Mgr](https://palm.wiki/development/docs/601/PalmOSReference/DataAndResourceManager.html), [Date & Time Mgr](https://palm.wiki/development/docs/601/PalmOSReference/DateAndTimeManager.html), [Lists](https://palm.wiki/development/docs/601/PalmOSReference/List.html)
- PilRC manual: [MIT mirror](https://web.mit.edu/pilot/src/prc-tools-0.4.2/pilrc.html)
- Emulator: [CloudpilotEmu](https://cloudpilot-emu.github.io/app/)
- SDK: [PalmDB SDK downloads](https://palmdb.net/app/palm-os-sdk), [archive.org/palm-os-sdk](https://archive.org/download/palm-os-sdk)
