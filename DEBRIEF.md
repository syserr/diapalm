# Diapalm for Palm OS — project debrief

A retrospective on building Diapalm, a classic Palm OS 68K diabetes diary
inspired by Diaguard (an Android app). Written at the end of the build, for
whoever picks this up next — including a future me who has forgotten all of
it.

**Outcome:** a working 13 KB PRC that logs entries, lists them, edits and
deletes them, estimates A1c, exports CSV, and fires a reminder. ~2,550 lines of
C plus a PilRC resource file. Every item in the plan's verification list was
exercised in a real Palm OS 4.1 emulator.

---

## 1. What was actually built

| Module | Lines | Job |
| --- | --- | --- |
| `app.c` | 277 | `PilotMain`, launch codes, event loop, menus, form navigation |
| `dpentry.c` | 344 | database lifecycle, record CRUD, sort, formatting/parsing |
| `dplist.c` | 272 | the entry list form |
| `dpedit.c` | 234 | new/edit entry form |
| `dpexport.c` | 146 | CSV export database |
| `dpalarm.c` | 57 | reminder set / cancel / fire |
| `dpa1c.c` | 719 | A1c estimate, lab-value history database, A1c form |
| `state.c` | 58 | per-launch state chunk |
| `startup.c` | 43 | entry stub + absolute relocations |
| headers + `app.rcp` | ~510 | record struct, FourCCs, resource IDs, UI resources |

Storage is deliberately dumb: one fixed-size 132-byte record per entry, kept
sorted oldest-first by timestamp. No heap, no parsing in draw callbacks, a 1:1
mapping to CSV rows. Backdated entries insert into the right place; display
walks the list backwards.

---

## 2. Decisions

**Classic 68K, not ARM.** ARM (Palm OS 5) would have meant a different
toolchain, a different runtime model, and a device class that is far rarer in
emulation. 68K runs on everything from Palm OS 3.5 through Garnet, and the
toolchain is mature.

**Core MVP scope.** Log entry, list, edit/delete, PDB storage, CSV export, one
reminder. Explicitly *out*: food database, graphs, statistics, tags, i18n, unit
customization, PDF export. The Android app's domain model maps to a diary; most
of the analysis features are a different project.

**A1c added on request.** A friend who wants to run this on a real Palm said the
A1c predictor was the reason they would use it — the app logged and exported but
"didn't tell you anything back". So the estimate ships, and so does manual entry
of a lab value, which Android supports but never displays as a history. The
label is `est. A1c`, never `HbA1c`: a computed number must not read as a lab
result. Lab values get their own 8-byte record type in a separate database
rather than a field on the diary record, which is pinned at 132 bytes by a
compile-time check. Integer math throughout — `(31*sum + 2393*n) / (100*n)`,
carrying the sum to avoid double rounding — because the 68K has no FPU.

**CSV via a PDB, not Beaming or Exchange Manager.** Export writes a
`Diapalm Export` database that any PDB tool can read. Chosen because it works
in the emulator — beaming and Exg both need infrastructure that isn't there
headless — and because it is trivially verifiable on the host side.

**Fixed-size records over variable-length.** A `note` field capped at 120 chars
and zero meaning "not recorded" removes all the record-length bookkeeping, makes
the sort index simple, and keeps draw callbacks allocation-free.

**State in a heap chunk published through a feature slot.** This was forced, not
chosen — see §4.1. Every module reaches for it through `DpStateGet()`.

**A two-stage link with generated relocations.** `generate-m68k-relocs.mjs`
scans the first link's relocations and emits assembly pinning the 68K trap
addresses for the second link. Not optional; skipping it produces a PRC that
crashes on launch.

**Module headers are `dp`-prefixed** (`dplist.h`, not `list.h`). A header named
`list.h` or `edit.h` shadows the SDK's `List.h` / `Edit.h` on macOS's
case-insensitive filesystem. This is the kind of thing that costs an hour if you
don't know it and zero if you do.

---

## 3. Tools

| Tool | Role |
| --- | --- |
| [palm-toolchain](https://github.com/palmsnipe/palm-toolchain) | `m68k-none-elf-gcc` 16.1.0, PilRC, `build-prc`, relocation generator |
| Palm OS SDK 5r4 (Garnet) | headers; the genuine SDK, not a reduced mirror |
| [CloudpilotEmu](https://cloudpilot-emu.github.io/) | built from source, driven headlessly |
| Palm OS 4.1 ROM on a Palm m515 | DragonBall VZ; the test target |

CloudpilotEmu was driven through its CLI over a FIFO with
`SDL_VIDEODRIVER=dummy` — `screenshot`, `install`, `launch`, `reset-soft`,
`pen X Y`. That harness lives in a separate `palm-testing/` directory, outside this repo.

**It works on Apple Silicon.** The toolchain is a native build; no Rosetta
involvement. That was an open question at the start and the answer is yes.

**`m68k-none-elf-gdb` is available** at `$(PREFIX)/bin/`, and CloudpilotEmu has
a GDB stub (`--listen <port>`, `--wait-for-attach`, `--debug-app <elf>`). I
never used it. In hindsight I should have — see §5.1.

---

## 4. The hard parts

### 4.1 The linker discards writable globals

The toolchain's `linker.lkr` drops `.data` and `.bss`, so **every** static and
global variable is silently absent from the image. This is not a link error and
not a warning. A global read returns garbage and a global write goes nowhere.

The fix is to keep all mutable state in one `MemPtrNew`'d chunk, publish its
address with `FtrSet`, and fetch it with `FtrGet`. `DpStateGet()` re-initialises
if the slot is empty. This is why `state.c` exists at all and why nothing in
this codebase is a global.

### 4.2 Record writes must go through `DmWrite`

Writing through the pointer returned by `DmNewRecord` / `DmGetRecord` crashes
the device. The pointer is only valid until the next `DmWrite`, `DmRemoveRecord`
or `DmReleaseRecord`, and the OS tracks dirty ranges — a raw store bypasses that
and corrupts the chunk. Every field write goes through `DmWrite`.

### 4.3 The alarm that never fired, and then fired fatally

This was the stubborn one, and it was really two bugs stacked.

**First: the alarm never fired at all.** `AlmSetAlarm(cardNo, dbID, ref, when,
quiet)` stores `dbID` *without validating it* and returns `errNone`. I had
passed the data database's `LocalID`. The parameter must identify the
**application** database — the `'appl'`-typed one that `SysAppLaunch` is handed
at fire time. A wrong value fails completely silently: no error, no alarm, no
log. The correct lookup is the one the SDK's own `AppCallWithCommand` macro
uses:

```c
DmGetNextDatabaseByTypeCreator(true, &searchState, sysFileTApplication,
                               DP_DB_CREATOR, true, &cardNo, &dbID);
```

**Second: once it fired, it crashed.** A bare `FatalException` — a 68K hardware
exception, i.e. a bad memory access, distinct from `SysFatalAlert` with a
message. No text, no stack, nothing to go on.

I bisected the wrong axis for a long time. I built variants to ask *"is it
EditForm? is it the list? is it the alarm launch?"* — and the results looked
contradictory:

| Observation | What I concluded |
| --- | --- |
| EditForm renders fine on a normal launch | EditForm is innocent |
| Opening the **list** instead on an alarm launch still crashed | confusing |
| Removing `DpAlarmSet(0)` from the fire handler stopped the crash | the alarm cancel is the bug |

The last one was a false lead that cost four builds and three ~6-minute
emulator runs. Removing one re-entrant call had removed *a* symptom, not the
cause.

**The actual bug was in the launch-code contract**, and the SDK header says it
outright:

> *"This action code is sent via a direct function call into the app's
> `PilotMain` function **without 'launching' the app**."* … *"Restrictions: No
> accessing of global or static variables unless `sysAppLaunchFlagSubCall` flag
> is set."*

When the reminder came due **while Diapalm was already running** — the normal
case — Palm OS did not launch the app again. It called `PilotMain` **directly,
on top of the running event loop**. Our code ignored `launchFlags`, so it:

1. allocated a **second** state chunk, orphaning the live one,
2. opened the database a **second** time,
3. ran a **nested** `AppEventLoop`,
4. and on exit freed the state chunk the outer loop was still using.

Step 4 is the bus error. Every form choice crashes identically, which is
exactly why the bisection was uninformative.

The fix, ~15 lines in `app.c`:

```c
if (launchFlags & sysAppLaunchFlagSubCall) {
    if (command == sysAppLaunchCmdDisplayAlarm) {
        EventType event;
        MemSet(&event, sizeof(event), 0);
        event.eType = DP_ALARM_EVENT;      /* firstUserEvent + 1 */
        EvtAddEventToQueue(&event);
    }
    return errNone;
}
```

with a matching case in `AppEventLoop` that opens the entry form. One event
loop, one state chunk, no re-entry.

**The lesson is the axis, not the symptom:** when a bug appears only on one
launch code, bisect `launchFlags`, not the UI. I had read that header comment
earlier in the project and not connected it. Reading it *first* would have
collapsed roughly five hours into twenty minutes.

### 4.4 A reset silently drops a pending reminder

Palm keeps the alarm table on the dynamic heap. It survives neither a soft reset
nor a clock change, and apps are expected to re-submit their alarms when they
receive `sysAppLaunchCmdSystemReset` (2) or `sysAppLaunchCmdTimeChange` (3).
This app handles neither, so a reminder set before a reset is lost. Logged
entries are unaffected. **Documented as a known limitation in `README.md`, not
fixed** — it is new functionality rather than part of the fire-cycle bug.

### 4.5 The emulator harness looked unreliable, and the diagnosis was wrong

I spent a long stretch believing synthetic pen events dropped unpredictably.
They do not. **Every one of those "dropped taps" was a screenshot taken too
early**, and the fix is a settle, not a workaround.

`pen` does not deliver an event. `CmdPen` calls `QueuePenEvent(down)` and
`QueuePenEvent(up)`, which park in `penEventQueueIncoming`; the emulator
promotes them to the queue PalmOS actually reads only inside
`EmPalmOS::DispatchNextEvent()`, and only once
`systemCycles - lastEventPromotedAt >= MIN_CYCLES_BETWEEN_EVENTS` (10000).
Screenshot before that finishes and you capture the **previous** frame — which
is pixel-identical to the app having ignored the tap. `harness/probe-timing.sh`
settled it: with a settle, the same tap produces the same screen hash 4/4 at
every settle from 1s to 4s.

(One refinement I got wrong at first and then measured: runs are *not*
byte-identical, because the app stamps the wall clock into every row. Diffing
two full runs pixel by pixel, the only differing pixels are the timestamp's
digits — a 4px-wide column, 8–25 pixels out of 25,600 — at x=46..49 on the list
and x=136..139 in the edit form's `When:` field. Behaviour is deterministic;
only the clock moves.)

Three more things that *are* real, and each of which I first misread as
flakiness:

- **The menu bar starts hidden and hides again every time you leave it.** It is
  not just a launch-time state. Each menu walk has to re-reveal it with a tap at
  `(20,8)`, which both reveals the bar and opens File; from there sliding along
  y=8 opens the neighbouring menu, the ordinary PalmOS idiom.
- **`type` silently loses characters.** It queues one key event per character
  into a 20-entry queue (`EVENT_QUEUE_SIZE = 20`) that makes room by discarding
  the **oldest** entry, so 26 characters arrive as the last 20 —
  `Ate pancakes for breakfast` landed as `nckes for breakfast`. Nothing reports
  the loss. Chunk to ≤12 characters with a settle between chunks.
- **Backspace can never be sent.** The CLI reads stdin through readline, which
  consumes control bytes as line-editing keys; `type "<BS>"` arrives as the
  mangled line `type"` → `invalid command type`. Clear a field the way a person
  would instead — drag across it and type over the top. The drag anchor must go
  down **past the end** of the text and back **past the start**; anchoring inside
  the text catches only the characters after the anchor.

**The escape hatch that still applies:** when the thing under test is app logic
rather than UI, don't drive the UI at all. Compile a temporary hook behind a
macro (the Makefile takes `EXTRA_CFLAGS=-D...`) that performs the setup in code
at launch, and run with **zero taps**. That is how the alarm path was first
exercised, and it remains the right tool for logic — but it is no longer needed
because the UI is unreliable. It isn't. `harness/demo.sh` now drives the whole
app through the UI and every beat changes the screen.

Related emulator quirks, both proven by bisection rather than assumption:

- The RTC seconds interrupt is unimplemented in all four CloudpilotEmu
  backends, yet `TimGetSeconds` is live and alarms **do** fire. Stock Date Book
  was the control that settled it. (I spent time suspecting our code here; the
  emulator was never at fault.)
- `SelectOneTime`'s minute-ones digit wraps 9→0 without carrying, and the
  displayed time is an open-time snapshot. Cosmetic, in the OS dialog, not ours.

### 4.6 Two smaller traps

- **`FrmCustomAlert` placeholders must be `^1`/`^2`/`^3`, never `^0`** — with
  `^0` the text renders blank.
- **`LstSetListChoices` clears the selection**, and the highlight repaints
  *after* your draw callback returns. Both surprise you exactly once.

---

## 5. Where the effort went

Wall clock: **~15 hours** end to end, including a 5h43m overnight idle gap, so
about **9h20m of actual work**.

The alarm thread alone ran **~5 hours** — roughly **a third of the calendar
time and half of the active work** — for a **15-line** change. Some of that was
the harness (44 scripts, 157 emulator runs; ~23 scripts and ~39 runs were
alarm-related), and some was the two *earlier* bugs on the same path (§4.3's
`dbID` fix and the RTC-tick detour).

That ratio is not as bad as it sounds: the bug had no message, no debugger
attached, and a launch code Palm documents as a footnote. But the avoidable part
is real, and it is §4.3's lesson.

### 5.1 Things I would do differently

1. **Read the launch-code contract before writing tests, not after.** The answer
   was in `SystemMgr.h` the whole time.
2. **Bisect the variable that actually differs.** `launchFlags` was the only
   thing different about the alarm path. I bisected the *UI* instead, which
   could never have been the answer because every form crashed.
3. **Attach the GDB stub.** CloudpilotEmu has one and the toolchain ships
   `m68k-none-elf-gdb`. A bare bus error is exactly what a debugger is for, and
   I spent hours inferring from screenshots what a backtrace would have shown
   immediately. This is the single biggest process miss on the project.
4. **Treat a partial fix as a signal to keep looking.** Removing
   `DpAlarmSet(0)` stopped the crash, and I believed it. It had removed one
   re-entrant call, not the structural re-entry.

---

## 6. What is verified, and what is not

**Exercised in a real emulator:** new entry → appears at top of list; backdated
entry sorts below; edit updates; delete (both menu and form paths) with
confirmation; note-only entry renders and exports; CSV export produces a
`Diapalm Export` database readable on the host and byte-diffable against
expectation; re-export replaces rather than accumulates; error paths (invalid
blood sugar, all-empty save, zero-entry export); set reminder → fires →
entry form opens pre-filled with the fire time; cancel reminder; reboot →
entries persist.

**The alarm fire cycle was verified twice** — once with test hooks, once again
on the cleaned-up build with every `DP_AUTOTEST_*` hook removed. Both opened the
Entry form pre-filled with the correct fire time, confirmed by reading the
screenshots rather than trusting hashes.

**Not verified / known gaps:**

- Reset and clock-change alarm re-submission (§4.4). Documented, not fixed.
- Two entries saved in the same second have no defined order between them.
- Timestamps are raw seconds, so data survives past 2031; only the 7-bit year
  *display* in some Palm date dialogs does not.
- PilRC warns about a missing Graffiti State Indicator on two forms
  (`app.rcp:160` and `:180`). Cosmetic; not addressed.

---

## 7. Notes for the next person

- **This project is untracked in git.** Nothing here is committed.
- The real build is **13,309 bytes**, 21 resources, validated by `make test`.
- The `EXTRA_CFLAGS` passthrough in the Makefile is a genuine improvement, not
  test scaffolding — it is how any launch-time hook gets compiled in.
- Harness scripts live in a separate `palm-testing/` directory, deliberately
  outside this repo, the toolchain and the emulator trees. Its
  `harness/fire-test.sh` is the alarm test. The older `palm-emu-test/` scripts
  (`fire-testB.sh` and friends) are historical.
- `fire-test8.sh` and friends are historical; several contain a dead
  `distinct frames` block from before the BSD-`uniq` fix. The per-step hashes in
  their output are still valid.
- **Screenshot hashes are not stable across runs** when a form displays the
  current time. Read the PNG; don't trust a hash you labelled earlier.

The four lessons worth knowing before touching this code are the launch-code
subcall re-entry, the `dbID` rule, the linker's discarded globals, and the
harness limits. They were kept in the assistant's project memory, which is not
part of this repo.
