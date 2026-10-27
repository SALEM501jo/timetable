# Course Timetable Scheduler

![CI](https://github.com/SALEM501jo/timetable/actions/workflows/ci.yml/badge.svg)

A small C++17 library and command-line tool that turns course, room and instructor CSV files into a
**conflict-free weekly timetable**. It enforces the academic rules a registration department works with
(room, instructor and student clashes, capacity, lab rooms, allowed meeting days, teaching load), explains
every section it could not place, and reports room utilisation and instructor load.

The scheduler's output is always checked by an **independent validator**, both in the CLI and in the tests.

## Sample output

`timetable_cli data` on the bundled sample data (21 sections, 8 rooms, 8 instructors, 12 students).
The sample deliberately includes three sections that cannot be placed:

```
warning: data\sample_courses.csv line 21, field 'enrolment': AI300-1 has 150 students but the largest room holds 120; it cannot be placed
warning: data\sample_courses.csv line 22, field 'enrolment': AI300L-1 has 40 students but the largest lab room holds 35; it cannot be placed
warning: data\sample_instructors.csv line 9, field 'max_weekly_hours': I08 is assigned 4h of sections but may teach at most 3h; some will be unplaced

TIMETABLE (18 of 21 sections placed, ordering: static)

Days         Time         Room   Section    Title                   Instructor         Students
-----------  -----------  -----  ---------  ----------------------  -----------------  --------
Sun          08:00-09:00  LAB1   CS201L-1   Data Structures Lab     Eng. Yousef Odeh   25
Sun          08:00-09:00  LAB2   CS101L-1   Programming Lab         Eng. Lina Haddad   24
Sun          08:00-09:00  LAB3   CS230L-1   Databases Lab           Eng. Kareem Zu'bi  30
Sun/Tue/Thu  09:00-10:00  HALL1  SE300-1    Software Engineering    Dr. Ahmad Khalil   45
...
Mon/Wed      08:00-09:00  R202   ENG101-1   Academic English        Dr. Sami Abbadi    30

UNPLACED SECTIONS

Section   Title                 Reason
--------  --------------------  ------------------------------------------------------------
AI300-1   Machine Learning      no room holds 150 students (largest holds 120)
AI300L-1  Machine Learning Lab  no lab room holds 40 students (largest holds 35)
ENG101-2  Academic English      instructor I08 would teach 4h a week, over the maximum of 3h

ROOM UTILISATION

Room   Type  Capacity  Sections  Time used  Seats filled
-----  ----  --------  --------  ---------  ------------
HALL1  room  120       2         13.3%      64.6%
LAB1   lab   25        2         4.4%       94.0%
...

INSTRUCTOR LOAD

Instructor  Name               Sections  Hours  Max
----------  -----------------  --------  -----  ---
I01         Dr. Ahmad Khalil   3         9h     12h
...

Validator: 0 violations.
```

## Build and run

Needs CMake 3.20+ and a C++17 compiler (GCC 9+, Clang 10+ or MSVC 2019+). Catch2 is downloaded by CMake.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

Run it (the CLI sits at `build/timetable_cli`, or `build/Release/timetable_cli.exe` with Visual Studio):

```bash
build/timetable_cli data                          # every .csv file in data/
build/timetable_cli data --out timetable.csv      # also export the timetable as CSV (opens in Excel)
build/timetable_cli data --order constrained      # most-constrained-first ordering
build/timetable_cli data --compare                # compare both orderings
build/timetable_cli courses.csv rooms.csv instructors.csv   # explicit files, any order
```

Exit codes: `0` done (unplaced sections are reported, not a failure), `2` bad input or usage,
`3` the validator found a violation (which would be a bug).

## CSV formats

Files are recognised by their header, so they can be passed in any order. Columns may be in any order,
extra columns are ignored, quoted fields (`"Khalil, Ahmad"`), CRLF line ends and a UTF-8 BOM are handled.

| File | Columns |
|------|---------|
| courses | `section_id, course_code, title, credits, instructor_id, enrolment, needs_lab` |
| rooms | `room_id, capacity, is_lab` |
| instructors | `instructor_id, name, max_weekly_hours` |
| registrations (optional) | `student_id, section_id` |

Booleans accept `true/false`, `yes/no` or `1/0`.

**Input validation.** Every problem is reported with its file, line and field, and all problems are
reported at once rather than stopping at the first:

```
error: courses.csv line 4, field 'credits': expected a whole number, got 'three'
error: courses.csv line 7, field 'section_id': duplicate 'CS101-1' (first defined on line 2)
error: courses.csv line 1, field 'enrolment': required column is missing
error: courses.csv line 9, field 'instructor_id': unknown instructor 'I99' (not in instructors.csv)
warning: courses.csv line 21, field 'enrolment': AI300-1 has 150 students but the largest room holds 120; it cannot be placed
```

Errors stop the run; warnings (sections that can never be placed, over-committed instructors, repeated
registrations) are shown and the run continues.

## Hard rules

| # | Rule |
|---|------|
| 1 | A room hosts at most one section at a time. |
| 2 | An instructor teaches at most one section at a time. |
| 3 | A section's room capacity is at least its enrolment. |
| 4 | A section that needs a lab is placed in a lab room. |
| 5 | A section meets only on an allowed pattern, for the right length, inside the teaching day. |
| 6 | An instructor's weekly teaching hours do not exceed their maximum. |
| 7 | A student is not registered in two sections that meet at the same time. |

**Time model** (one `TimeConfig` struct in [models.hpp](include/timetable/models.hpp)): Sunday to Thursday,
08:00 to 17:00 in 30-minute slots. A section meets `credits × 60` minutes a week. Lectures split that evenly
over **Sun/Tue/Thu** or **Mon/Wed** when it fits whole slots (a 3-credit course: 1 h × 3 or 1.5 h × 2;
a 2-credit course: Mon/Wed 1 h). Labs meet once a week on any one day.

## Design

```
            CSV files
                |
          +-----v-----+     Issue list (file, line, field, message)
          |    csv    |---> errors stop the run, warnings are shown
          +-----+-----+
                |  Problem (sections, rooms, instructors, registrations, TimeConfig)
        +-------+--------+
        |                |
  +-----v-----+    +-----v------+
  | scheduler |--->| validator  |   validator knows nothing about the scheduler
  +-----+-----+    +-----+------+
        |  Timetable     |  Violations
        +-------+--------+
                |
          +-----v-----+
          |  report   |---> console tables, CSV export
          +-----------+
```

| Module | Responsibility |
|--------|----------------|
| [models](include/timetable/models.hpp) | Plain value types, the time model, and `allowed_patterns` (rule 5 in one place). |
| [csv](include/timetable/csv.hpp) | RFC 4180 parsing, typed loading with line-numbered issues, cross-file checks. |
| [validator](include/timetable/validator.hpp) | Finds every hard-rule violation in any timetable. |
| [scheduler](include/timetable/scheduler.hpp) | Builds a timetable and explains each unplaced section. |
| [report](include/timetable/report.hpp) | Room utilisation, instructor load, console tables, CSV export. |

Design choices:

- **Validator independent of the scheduler ("trust but verify").** The scheduler tracks bookings in
  per-resource busy grids; the validator compares every pair of assignments directly. Because they are
  written differently, a bug in one is caught by the other. A property test runs both on 100 generated
  problems and requires zero violations.
- **Value types and RAII only**: `std::vector`, `std::string`, `std::optional`, `std::unordered_map`.
  No raw `new`/`delete`, no globals.
- **Errors are data**: loaders return the valid rows plus a list of `Issue`s, so a user sees every problem
  in a file at once.
- **Deterministic**: fixed orderings and tie-breaks everywhere (tested by running twice and comparing).

### Algorithm

Greedy, one section at a time:

1. Order the sections: labs first (lab rooms are scarce), then largest enrolment first, then by id.
2. For each section, try every option in a fixed order: meeting pattern, then start slot, then room.
   Rooms are tried best fit first (ordinary rooms before labs, smallest adequate room first), so large rooms
   and labs stay free for the sections that need them.
3. Take the first option where the room, instructor and every registered student are free and the
   instructor stays within their weekly maximum.
4. If there is none, record the section as unplaced with the first rule that blocks it: unknown
   instructor, no pattern fits the credits, no room is big enough, load limit, instructor never free,
   no time when instructor and students are all free, or every suitable room booked.

`--order constrained` instead picks, at each step, the remaining section with the fewest free options
(ties fall back to the order above).

**What I measured.** On the sample data both orderings place 18 of 21 sections (the other 3 are
impossible). On the 100 generated problems from the property test (1,728 sections, many deliberately
over-subscribed), static ordering placed **762** sections and most-constrained-first placed **744**: it did
better on 5 problems and worse on 16. A plausible reason is that "fewest options right now" ignores
instructor load limits, so it can spend an instructor's hours on a section that had many other options.
Static ordering stays the default.

## Tests

45 Catch2 test cases (`ctest` runs each one separately):

- **CSV**: quoting, CRLF/BOM, line numbers, valid files, missing column, duplicate id, bad number,
  negative values, bad boolean, wrong field count, unknown instructor, impossible section, the sample data.
- **Validator**: one test per hard rule that triggers it, a clean timetable, back-to-back sections,
  bad references.
- **Scheduler**: a solvable case, impossible sections, an instructor limit forcing an unplaced section,
  student clash avoidance, each unplaced reason, determinism.
- **Property**: 100 generated problems × 2 orderings, always zero violations, every section placed or
  unplaced exactly once.
- **Reports**: utilisation, load, CSV escaping and export, time helpers, allowed patterns.

CI builds with warnings as errors (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, `/W4` on MSVC) and
runs the tests and the CLI on Ubuntu and Windows.

## Limitations and next steps

- **Greedy, no backtracking.** A section that is unplaced might fit if an earlier one were moved. Next step:
  bounded backtracking or a repair pass that tries moving one blocking section.
- **No soft constraints.** Instructor preferences, spreading a student's day, or avoiding early starts are
  not scored. Everything starts as early as possible, so the timetable bunches up in the mornings.
- **Fixed patterns.** Lectures use only Sun/Tue/Thu or Mon/Wed; a 1-credit lecture becomes 30 minutes
  twice a week. The patterns are configurable in code (`TimeConfig`) but not from a file yet.
- **One instructor per section**, no cross-listed sections, no room features beyond "lab".
- **Scale.** The validator is O(n²) in assignments and most-constrained ordering is O(n²) in sections.
  Fine for a department (hundreds of sections), not tuned for a whole university.
