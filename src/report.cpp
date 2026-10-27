#include "timetable/report.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <ostream>
#include <sstream>

namespace timetable {

namespace {

using TextRow = std::vector<std::string>;

void print_table(std::ostream& out, const TextRow& header, const std::vector<TextRow>& rows) {
    std::vector<std::size_t> width(header.size());
    for (std::size_t c = 0; c < header.size(); ++c) width[c] = header[c].size();
    for (const auto& row : rows) {
        for (std::size_t c = 0; c < row.size() && c < width.size(); ++c) width[c] = std::max(width[c], row[c].size());
    }

    auto print_row = [&](const TextRow& row) {
        for (std::size_t c = 0; c < row.size(); ++c) {
            out << row[c];
            if (c + 1 < row.size()) out << std::string(width[c] - row[c].size() + 2, ' ');
        }
        out << '\n';
    };
    print_row(header);
    TextRow rule;
    for (std::size_t w : width) rule.push_back(std::string(w, '-'));
    print_row(rule);
    for (const auto& row : rows) print_row(row);
}

std::string percent(double fraction) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << fraction * 100.0 << '%';
    return out.str();
}

// Reading order for a week: by first day, then start time, then room.
std::vector<const Assignment*> in_week_order(const Timetable& timetable) {
    std::vector<const Assignment*> sorted;
    for (const auto& assignment : timetable.assignments) sorted.push_back(&assignment);
    std::sort(sorted.begin(), sorted.end(), [](const Assignment* a, const Assignment* b) {
        const int day_a = a->days.empty() ? 0 : a->days.front();
        const int day_b = b->days.empty() ? 0 : b->days.front();
        if (day_a != day_b) return day_a < day_b;
        if (a->start_slot != b->start_slot) return a->start_slot < b->start_slot;
        if (a->room_id != b->room_id) return a->room_id < b->room_id;
        return a->section_id < b->section_id;
    });
    return sorted;
}

}  // namespace

std::vector<RoomUsage> room_usage(const Problem& problem, const Timetable& timetable) {
    const TimeConfig& config = problem.config;
    std::vector<RoomUsage> usage;
    for (const auto& room : problem.rooms) {
        RoomUsage u;
        u.room_id = room.id;
        u.capacity = room.capacity;
        u.is_lab = room.is_lab;
        u.total_slots = config.day_count() * config.slots_per_day();
        double fill_sum = 0.0;
        for (const auto& assignment : timetable.assignments) {
            if (assignment.room_id != room.id) continue;
            ++u.sections;
            u.used_slots += static_cast<int>(assignment.days.size()) * assignment.length_slots;
            if (const Section* section = problem.find_section(assignment.section_id)) {
                fill_sum += static_cast<double>(section->enrolment) / room.capacity;
            }
        }
        if (u.total_slots > 0) u.utilisation = static_cast<double>(u.used_slots) / u.total_slots;
        if (u.sections > 0) u.seat_fill = fill_sum / u.sections;
        usage.push_back(u);
    }
    std::sort(usage.begin(), usage.end(), [](const RoomUsage& a, const RoomUsage& b) { return a.room_id < b.room_id; });
    return usage;
}

std::vector<InstructorLoad> instructor_loads(const Problem& problem, const Timetable& timetable) {
    std::vector<InstructorLoad> loads;
    for (const auto& instructor : problem.instructors) {
        InstructorLoad load;
        load.instructor_id = instructor.id;
        load.name = instructor.name;
        load.max_minutes = instructor.max_weekly_hours * 60;
        for (const auto& assignment : timetable.assignments) {
            const Section* section = problem.find_section(assignment.section_id);
            if (section == nullptr || section->instructor_id != instructor.id) continue;
            ++load.sections;
            load.minutes += weekly_minutes(assignment, problem.config);
        }
        loads.push_back(load);
    }
    std::sort(loads.begin(), loads.end(),
              [](const InstructorLoad& a, const InstructorLoad& b) { return a.instructor_id < b.instructor_id; });
    return loads;
}

void print_timetable(std::ostream& out, const Problem& problem, const Timetable& timetable) {
    const TimeConfig& config = problem.config;
    std::vector<TextRow> rows;
    for (const Assignment* a : in_week_order(timetable)) {
        const Section* section = problem.find_section(a->section_id);
        const Instructor* instructor = section ? problem.find_instructor(section->instructor_id) : nullptr;
        rows.push_back({format_days(a->days, config),
                        config.slot_time(a->start_slot) + "-" + config.slot_time(a->end_slot()), a->room_id,
                        a->section_id, section ? section->title : "?", instructor ? instructor->name : "?",
                        section ? std::to_string(section->enrolment) : "?"});
    }
    print_table(out, {"Days", "Time", "Room", "Section", "Title", "Instructor", "Students"}, rows);
}

void print_unplaced(std::ostream& out, const Problem& problem, const std::vector<Unplaced>& unplaced) {
    if (unplaced.empty()) {
        out << "Every section was placed.\n";
        return;
    }
    std::vector<TextRow> rows;
    for (const auto& u : unplaced) {
        const Section* section = problem.find_section(u.section_id);
        rows.push_back({u.section_id, section ? section->title : "?", u.reason});
    }
    print_table(out, {"Section", "Title", "Reason"}, rows);
}

void print_room_usage(std::ostream& out, const std::vector<RoomUsage>& usage) {
    std::vector<TextRow> rows;
    for (const auto& u : usage) {
        rows.push_back({u.room_id, u.is_lab ? "lab" : "room", std::to_string(u.capacity), std::to_string(u.sections),
                        percent(u.utilisation), u.sections > 0 ? percent(u.seat_fill) : "-"});
    }
    print_table(out, {"Room", "Type", "Capacity", "Sections", "Time used", "Seats filled"}, rows);
}

void print_instructor_loads(std::ostream& out, const std::vector<InstructorLoad>& loads) {
    std::vector<TextRow> rows;
    for (const auto& load : loads) {
        rows.push_back({load.instructor_id, load.name, std::to_string(load.sections), format_hours(load.minutes),
                        format_hours(load.max_minutes)});
    }
    print_table(out, {"Instructor", "Name", "Sections", "Hours", "Max"}, rows);
}

void print_violations(std::ostream& out, const std::vector<Violation>& violations) {
    if (violations.empty()) {
        out << "Validator: 0 violations.\n";
        return;
    }
    out << "Validator: " << violations.size() << " violation(s)\n";
    for (const auto& v : violations) out << "  [" << rule_name(v.rule) << "] " << v.message << '\n';
}

std::string csv_field(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') quoted += '"';
        quoted += ch;
    }
    quoted += '"';
    return quoted;
}

void write_timetable_csv(std::ostream& out, const Problem& problem, const Timetable& timetable) {
    const TimeConfig& config = problem.config;
    out << "section_id,course_code,title,instructor_id,instructor_name,room_id,days,start,end,enrolment,capacity\r\n";
    for (const Assignment* a : in_week_order(timetable)) {
        const Section* section = problem.find_section(a->section_id);
        const Room* room = problem.find_room(a->room_id);
        const Instructor* instructor = section ? problem.find_instructor(section->instructor_id) : nullptr;
        const TextRow fields{a->section_id,
                             section ? section->course_code : "",
                             section ? section->title : "",
                             section ? section->instructor_id : "",
                             instructor ? instructor->name : "",
                             a->room_id,
                             format_days(a->days, config),
                             config.slot_time(a->start_slot),
                             config.slot_time(a->end_slot()),
                             section ? std::to_string(section->enrolment) : "",
                             room ? std::to_string(room->capacity) : ""};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) out << ',';
            out << csv_field(fields[i]);
        }
        out << "\r\n";
    }
}

}  // namespace timetable
