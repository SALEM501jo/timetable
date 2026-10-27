#pragma once

#include <string>
#include <vector>

namespace timetable {

// A named set of weekdays, e.g. "Sun/Tue/Thu". Days index TimeConfig::day_names.
struct DayPattern {
    std::string name;
    std::vector<int> days;
};

// The whole time model lives here so it is easy to change in one place.
struct TimeConfig {
    std::vector<std::string> day_names{"Sun", "Mon", "Tue", "Wed", "Thu"};
    int day_start_minutes = 8 * 60;
    int day_end_minutes = 17 * 60;
    int slot_minutes = 30;
    int minutes_per_credit = 60;  // weekly contact minutes per credit hour

    // Lectures split their weekly minutes evenly over one of these patterns.
    // Labs always meet once a week, on any single day.
    std::vector<DayPattern> lecture_patterns{{"Sun/Tue/Thu", {0, 2, 4}}, {"Mon/Wed", {1, 3}}};

    int day_count() const { return static_cast<int>(day_names.size()); }
    int slots_per_day() const { return (day_end_minutes - day_start_minutes) / slot_minutes; }
    std::string day_name(int day) const;    // "Sun", or "day 7" if out of range
    std::string slot_time(int slot) const;  // "HH:MM" at the start of a slot
};

struct Section {
    std::string id;
    std::string course_code;
    std::string title;
    int credits = 0;
    std::string instructor_id;
    int enrolment = 0;
    bool needs_lab = false;
    int source_line = 0;  // line in the input file, 0 if built in code
};

struct Room {
    std::string id;
    int capacity = 0;
    bool is_lab = false;
    int source_line = 0;
};

struct Instructor {
    std::string id;
    std::string name;
    int max_weekly_hours = 0;
    int source_line = 0;
};

struct Registration {
    std::string student_id;
    std::string section_id;
    int source_line = 0;
};

// Everything the scheduler and validator need to know.
struct Problem {
    std::vector<Section> sections;
    std::vector<Room> rooms;
    std::vector<Instructor> instructors;
    std::vector<Registration> registrations;
    TimeConfig config;

    const Section* find_section(const std::string& id) const;
    const Room* find_room(const std::string& id) const;
    const Instructor* find_instructor(const std::string& id) const;
};

// One way a section is allowed to meet: which days, and how long each meeting is.
struct MeetingPattern {
    std::string name;
    std::vector<int> days;  // sorted
    int length_slots = 0;
};

// The meeting patterns rule 5 allows for a section. Empty if none fit its credits.
std::vector<MeetingPattern> allowed_patterns(const Section& section, const TimeConfig& config);

// A section placed in a room, meeting on `days` from start_slot for length_slots.
struct Assignment {
    std::string section_id;
    std::string room_id;
    std::vector<int> days;
    int start_slot = 0;
    int length_slots = 0;

    int end_slot() const { return start_slot + length_slots; }
};

struct Timetable {
    std::vector<Assignment> assignments;

    const Assignment* find(const std::string& section_id) const;
};

// True when two assignments share a day and their time ranges intersect.
bool overlaps(const Assignment& a, const Assignment& b);
int weekly_minutes(const Assignment& assignment, const TimeConfig& config);

std::string format_days(const std::vector<int>& days, const TimeConfig& config);  // "Sun/Tue/Thu"
std::string format_time(const Assignment& assignment, const TimeConfig& config);  // "Sun/Tue/Thu 08:00-09:00"
std::string format_hours(int minutes);                                            // "3h", "4.5h"

}  // namespace timetable
