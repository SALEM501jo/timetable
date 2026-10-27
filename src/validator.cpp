#include "timetable/validator.hpp"

#include <algorithm>
#include <climits>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace timetable {

const char* rule_name(Rule rule) {
    switch (rule) {
        case Rule::RoomClash: return "room clash";
        case Rule::InstructorClash: return "instructor clash";
        case Rule::RoomCapacity: return "room capacity";
        case Rule::LabRoom: return "lab room";
        case Rule::AllowedTime: return "allowed time";
        case Rule::InstructorLoad: return "instructor load";
        case Rule::StudentClash: return "student clash";
        case Rule::BadReference: return "bad reference";
    }
    return "unknown";
}

namespace {

// An assignment whose section and room both exist.
struct Placed {
    const Assignment* assignment;
    const Section* section;
    const Room* room;
};

bool matches_allowed_pattern(const Section& section, const Assignment& assignment, const TimeConfig& config) {
    if (assignment.start_slot < 0 || assignment.end_slot() > config.slots_per_day()) return false;
    std::vector<int> days = assignment.days;
    std::sort(days.begin(), days.end());
    for (const auto& pattern : allowed_patterns(section, config)) {
        if (pattern.days == days && pattern.length_slots == assignment.length_slots) return true;
    }
    return false;
}

// Where two overlapping assignments first meet, e.g. "Sun 08:00".
std::string first_overlap(const Assignment& a, const Assignment& b, const TimeConfig& config) {
    int day = INT_MAX;
    for (int d : a.days) {
        if (std::find(b.days.begin(), b.days.end(), d) != b.days.end()) day = std::min(day, d);
    }
    return config.day_name(day) + ' ' + config.slot_time(std::max(a.start_slot, b.start_slot));
}

}  // namespace

std::vector<Violation> validate(const Problem& problem, const Timetable& timetable) {
    const TimeConfig& config = problem.config;
    std::vector<Violation> violations;
    auto report = [&](Rule rule, std::string message, std::vector<std::string> ids) {
        violations.push_back({rule, std::move(message), std::move(ids)});
    };

    // Checks on one assignment at a time.
    std::vector<Placed> placed;
    std::unordered_set<std::string> seen;
    for (const auto& assignment : timetable.assignments) {
        const std::string& id = assignment.section_id;
        const Section* section = problem.find_section(id);
        const Room* room = problem.find_room(assignment.room_id);
        if (section == nullptr) {
            report(Rule::BadReference, "unknown section '" + id + "'", {id});
            continue;
        }
        if (!seen.insert(id).second) {
            report(Rule::BadReference, id + " is assigned more than once", {id});
            continue;
        }
        if (room == nullptr) {
            report(Rule::BadReference, id + " is in unknown room '" + assignment.room_id + "'", {id});
            continue;
        }
        if (problem.find_instructor(section->instructor_id) == nullptr) {
            report(Rule::BadReference, id + " has unknown instructor '" + section->instructor_id + "'", {id});
        }
        if (room->capacity < section->enrolment) {
            report(Rule::RoomCapacity,
                   id + " has " + std::to_string(section->enrolment) + " students but room " + room->id + " holds " +
                       std::to_string(room->capacity),
                   {id});
        }
        if (section->needs_lab && !room->is_lab) {
            report(Rule::LabRoom, id + " needs a lab but " + room->id + " is not a lab room", {id});
        }
        if (!matches_allowed_pattern(*section, assignment, config)) {
            report(Rule::AllowedTime,
                   id + " meets " + format_time(assignment, config) + ", which is not allowed for a " +
                       std::to_string(section->credits) + "-credit " + (section->needs_lab ? "lab" : "lecture"),
                   {id});
        }
        placed.push_back({&assignment, section, room});
    }

    // Clashes between every pair of assignments. Deliberately simple and independent
    // of the scheduler's grid bookkeeping, so the two can check each other.
    for (std::size_t i = 0; i < placed.size(); ++i) {
        for (std::size_t j = i + 1; j < placed.size(); ++j) {
            const Placed& a = placed[i];
            const Placed& b = placed[j];
            if (!overlaps(*a.assignment, *b.assignment)) continue;
            const std::string when = first_overlap(*a.assignment, *b.assignment, config);
            const std::vector<std::string> ids{a.section->id, b.section->id};
            if (a.room->id == b.room->id) {
                report(Rule::RoomClash,
                       "room " + a.room->id + " is double-booked at " + when + ": " + ids[0] + " and " + ids[1], ids);
            }
            if (a.section->instructor_id == b.section->instructor_id) {
                report(Rule::InstructorClash,
                       "instructor " + a.section->instructor_id + " teaches " + ids[0] + " and " + ids[1] +
                           " at the same time (" + when + ")",
                       ids);
            }
        }
    }

    // Weekly load per instructor (ordered map, so output is in id order).
    std::map<std::string, std::pair<int, std::vector<std::string>>> load;
    for (const auto& p : placed) {
        auto& entry = load[p.section->instructor_id];
        entry.first += weekly_minutes(*p.assignment, config);
        entry.second.push_back(p.section->id);
    }
    for (const auto& [instructor_id, entry] : load) {
        const Instructor* instructor = problem.find_instructor(instructor_id);
        if (instructor != nullptr && entry.first > instructor->max_weekly_hours * 60) {
            report(Rule::InstructorLoad,
                   "instructor " + instructor_id + " teaches " + format_hours(entry.first) +
                       " a week, over the maximum of " + std::to_string(instructor->max_weekly_hours) + "h",
                   entry.second);
        }
    }

    // Student clashes: compare the placed sections of each student.
    std::unordered_map<std::string, std::size_t> index_of;
    for (std::size_t i = 0; i < placed.size(); ++i) index_of.emplace(placed[i].section->id, i);
    std::map<std::string, std::vector<std::size_t>> by_student;
    for (const auto& registration : problem.registrations) {
        const auto it = index_of.find(registration.section_id);
        if (it != index_of.end()) by_student[registration.student_id].push_back(it->second);
    }
    for (auto& [student, indexes] : by_student) {
        std::sort(indexes.begin(), indexes.end());
        indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
        for (std::size_t i = 0; i < indexes.size(); ++i) {
            for (std::size_t j = i + 1; j < indexes.size(); ++j) {
                const Placed& a = placed[indexes[i]];
                const Placed& b = placed[indexes[j]];
                if (!overlaps(*a.assignment, *b.assignment)) continue;
                report(Rule::StudentClash,
                       "student " + student + " has " + a.section->id + " and " + b.section->id +
                           " at the same time (" + first_overlap(*a.assignment, *b.assignment, config) + ")",
                       {a.section->id, b.section->id});
            }
        }
    }
    return violations;
}

}  // namespace timetable
