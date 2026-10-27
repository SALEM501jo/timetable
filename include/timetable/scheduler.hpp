#pragma once

#include <string>
#include <vector>

#include "timetable/models.hpp"

namespace timetable {

enum class Ordering {
    Static,           // labs first, then largest enrolment, then section id
    MostConstrained,  // next section is the one with the fewest free options right now
};

struct SchedulerOptions {
    Ordering ordering = Ordering::Static;
};

struct Unplaced {
    std::string section_id;
    std::string reason;
};

struct ScheduleResult {
    Timetable timetable;            // sorted by section id
    std::vector<Unplaced> unplaced;  // sorted by section id
};

// Greedy scheduler: takes sections one at a time and gives each the first free
// (meeting pattern, start slot, room) option that breaks no hard rule. Sections
// with no option are reported as unplaced with the reason. Same input, same output.
ScheduleResult schedule(const Problem& problem, const SchedulerOptions& options = {});

}  // namespace timetable
