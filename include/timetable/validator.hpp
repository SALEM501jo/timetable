#pragma once

#include <string>
#include <vector>

#include "timetable/models.hpp"

namespace timetable {

// The hard rules. Each one maps to a numbered academic regulation in the README.
enum class Rule {
    RoomClash,        // 1. a room hosts at most one section at a time
    InstructorClash,  // 2. an instructor teaches at most one section at a time
    RoomCapacity,     // 3. room capacity >= enrolment
    LabRoom,          // 4. lab sections go in lab rooms
    AllowedTime,      // 5. allowed days, duration and hours only
    InstructorLoad,   // 6. instructor weekly hours <= maximum
    StudentClash,     // 7. a student has at most one section at a time
    BadReference,     // unknown section/room/instructor, or a section assigned twice
};

const char* rule_name(Rule rule);

struct Violation {
    Rule rule;
    std::string message;
    std::vector<std::string> section_ids;
};

// Finds every hard-rule violation in a timetable. It knows nothing about how the
// timetable was built, so it can check the scheduler (or a hand-made timetable).
// Sections that are not in the timetable are not violations: they are unplaced.
std::vector<Violation> validate(const Problem& problem, const Timetable& timetable);

}  // namespace timetable
