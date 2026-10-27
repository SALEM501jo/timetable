#include "timetable/models.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace timetable {

namespace {

template <typename T>
const T* find_by_id(const std::vector<T>& items, const std::string& id) {
    for (const auto& item : items) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

}  // namespace

std::string TimeConfig::day_name(int day) const {
    if (day < 0 || day >= day_count()) return "day " + std::to_string(day);
    return day_names[static_cast<std::size_t>(day)];
}

std::string TimeConfig::slot_time(int slot) const {
    const int minutes = day_start_minutes + slot * slot_minutes;
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << minutes / 60 << ':' << std::setw(2) << minutes % 60;
    return out.str();
}

const Section* Problem::find_section(const std::string& id) const { return find_by_id(sections, id); }
const Room* Problem::find_room(const std::string& id) const { return find_by_id(rooms, id); }
const Instructor* Problem::find_instructor(const std::string& id) const { return find_by_id(instructors, id); }

std::vector<MeetingPattern> allowed_patterns(const Section& section, const TimeConfig& config) {
    std::vector<MeetingPattern> patterns;
    if (section.credits <= 0 || config.slot_minutes <= 0) return patterns;
    const int weekly = section.credits * config.minutes_per_credit;

    // A pattern fits when the weekly minutes split evenly into whole slots per meeting.
    auto add = [&](const std::string& name, std::vector<int> days) {
        const int meetings = static_cast<int>(days.size());
        if (meetings == 0 || weekly % meetings != 0) return;
        const int per_meeting = weekly / meetings;
        if (per_meeting % config.slot_minutes != 0) return;
        const int length = per_meeting / config.slot_minutes;
        if (length > config.slots_per_day()) return;
        std::sort(days.begin(), days.end());
        patterns.push_back({name, std::move(days), length});
    };

    if (section.needs_lab) {
        for (int day = 0; day < config.day_count(); ++day) add(config.day_name(day), {day});
    } else {
        for (const auto& pattern : config.lecture_patterns) add(pattern.name, pattern.days);
    }
    return patterns;
}

const Assignment* Timetable::find(const std::string& section_id) const {
    for (const auto& assignment : assignments) {
        if (assignment.section_id == section_id) return &assignment;
    }
    return nullptr;
}

bool overlaps(const Assignment& a, const Assignment& b) {
    if (a.start_slot >= b.end_slot() || b.start_slot >= a.end_slot()) return false;
    for (int day : a.days) {
        if (std::find(b.days.begin(), b.days.end(), day) != b.days.end()) return true;
    }
    return false;
}

int weekly_minutes(const Assignment& assignment, const TimeConfig& config) {
    return static_cast<int>(assignment.days.size()) * assignment.length_slots * config.slot_minutes;
}

std::string format_days(const std::vector<int>& days, const TimeConfig& config) {
    std::string out;
    for (int day : days) {
        if (!out.empty()) out += '/';
        out += config.day_name(day);
    }
    return out;
}

std::string format_time(const Assignment& assignment, const TimeConfig& config) {
    return format_days(assignment.days, config) + ' ' + config.slot_time(assignment.start_slot) + '-' +
           config.slot_time(assignment.end_slot());
}

std::string format_hours(int minutes) {
    std::ostringstream out;
    if (minutes % 60 == 0) {
        out << minutes / 60;
    } else {
        out << std::fixed << std::setprecision(1) << minutes / 60.0;
    }
    out << 'h';
    return out.str();
}

}  // namespace timetable
