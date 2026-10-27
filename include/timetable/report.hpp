#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "timetable/models.hpp"
#include "timetable/scheduler.hpp"
#include "timetable/validator.hpp"

namespace timetable {

struct RoomUsage {
    std::string room_id;
    int capacity = 0;
    bool is_lab = false;
    int sections = 0;
    int used_slots = 0;   // slot-days booked in the week
    int total_slots = 0;  // slot-days available in the week
    double utilisation = 0.0;  // used_slots / total_slots
    double seat_fill = 0.0;    // average enrolment / capacity over the room's sections
};

struct InstructorLoad {
    std::string instructor_id;
    std::string name;
    int sections = 0;
    int minutes = 0;
    int max_minutes = 0;
};

std::vector<RoomUsage> room_usage(const Problem& problem, const Timetable& timetable);
std::vector<InstructorLoad> instructor_loads(const Problem& problem, const Timetable& timetable);

void print_timetable(std::ostream& out, const Problem& problem, const Timetable& timetable);
void print_unplaced(std::ostream& out, const Problem& problem, const std::vector<Unplaced>& unplaced);
void print_room_usage(std::ostream& out, const std::vector<RoomUsage>& usage);
void print_instructor_loads(std::ostream& out, const std::vector<InstructorLoad>& loads);
void print_violations(std::ostream& out, const std::vector<Violation>& violations);

// Quotes a CSV field when it contains a comma, quote or line break.
std::string csv_field(const std::string& value);

// One row per placed section, CRLF line ends so it opens cleanly in Excel.
void write_timetable_csv(std::ostream& out, const Problem& problem, const Timetable& timetable);

}  // namespace timetable
