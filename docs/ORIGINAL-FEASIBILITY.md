# Diapalm on Palm OS — Feasibility & Plan

> **Historical.** An early feasibility note, kept for context. See `README.md` for the current state.

Short answer: **yes, I think so — with one big caveat about scope, and one decision you need to make.**

## What I know about Palm OS development

- **The language is C (or C++)**, using the Palm OS API: forms/lists/buttons via `Frm`/`List`/`Btn`, storage via the PDB record API (`Dm*`/`Pdb*`), reminders via `SysAddAlarm`, custom drawing for graphs via `Wnd`. The UI layout lives in `.rc` resource files compiled into the `.prc` with PilRC. I'm comfortable working with this API, and the full [official API reference is still online](https://palm.wiki/dev) if I need to look anything up.
- **There's a modern toolchain that runs on macOS** — [palmsnipe/palm-toolchain](https://github.com/palmsnipe/palm-toolchain) (GCC 16 for 68K + ARM, PilRC, PRC packaging, one-command setup) — so we can build on this machine and test in the [Palm OS Emulator](https://prc-tools.sourceforge.net) without a Windows box. Alternatives: legacy [prc-tools](https://prc-tools.sourceforge.net) or DmitryGr's modern GCC.
- The alternative language is **Mojo** (JavaScript, Palm OS 4.0+), but for a friend who wants to "write it by hand," classic C is the real Palm experience.

## The caveat: Diaguard is a big app

It has 9 tracked data types, a food database with thousands of entries, PDF/CSV export, backup, reminders, graphs, statistics, and 7 languages. A 1:1 port to a 160×160 (or 320×320) greyscale/color screen would be a months-long project, and some features (rich statistics, MPAndroidChart-style graphs) need rethinking on a tiny screen.

**Good fit for Palm OS:** core logging (glucose, insulin, carbs, etc.), the food DB (a PDB is ideal for thousands of records), reminders (native alarms), CSV export, and backup via hot-sync (PDB copy). **Needs work:** graphs (custom `Wnd` drawing, feasible but manual), PDF export (doable by hand — PDF is a text format — but the least interesting part to hand-write).

## My suggested path

1. Set up `palm-toolchain` + emulator on this Mac and get a hello-world `.prc` running in it.
2. Build the MVP: log entries (glucose/insulin/carbs) with a list view and a PDB store, plus a CSV export.
3. Add alarms (reminders), food DB, graphs, and PDF export in that order.

## Decisions needed before starting

- **Which device is the friend targeting** — classic 68K (m68k) is what the emulator runs most natively and is the "authentic" choice.
- **1:1 feature port or MVP-first approach** — MVP-first is strongly recommended; it's far more satisfying when it actually runs on real hardware.

## Sources

- [palmsnipe/palm-toolchain](https://github.com/palmsnipe/palm-toolchain)
- [prc-tools](https://prc-tools.sourceforge.net)
- [Palm OS Wiki — development resources](https://palm.wiki/dev)
- [Palm file format specification (PDB/PRC)](https://palmdb.net)
