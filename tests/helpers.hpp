#pragma once

#include <algorithm>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "timetable/csv.hpp"
#include "timetable/models.hpp"
#include "timetable/validator.hpp"

namespace helpers {

using namespace timetable;

inline Section make_section(const std::string& id, const std::string& instructor, int enrolment, int credits = 3,
                            bool lab = false) {
    Section section;
    section.id = id;
    section.course_code = id.substr(0, id.find('-'));
    section.title = "Course " + id;
    section.credits = credits;
    section.instructor_id = instructor;
    section.enrolment = enrolment;
    section.needs_lab = lab;
    return section;
}

inline Room make_room(const std::string& id, int capacity, bool lab = false) {
    Room room;
    room.id = id;
    room.capacity = capacity;
    room.is_lab = lab;
    return room;
}

inline Instructor make_instructor(const std::string& id, int max_hours = 20) {
    Instructor instructor;
    instructor.id = id;
    instructor.name = "Instructor " + id;
    instructor.max_weekly_hours = max_hours;
    return instructor;
}

inline Assignment make_assignment(const std::string& section, const std::string& room, std::vector<int> days,
                                  int start, int length) {
    return Assignment{section, room, std::move(days), start, length};
}

inline CsvTable table_from(const std::string& text, std::vector<Issue>& issues) {
    std::istringstream in(text);
    return parse_csv(in, "test.csv", issues);
}

inline bool any_issue_mentions(const std::vector<Issue>& issues, const std::string& needle) {
    return std::any_of(issues.begin(), issues.end(), [&](const Issue& issue) {
        return to_string(issue).find(needle) != std::string::npos;
    });
}

inline std::size_t count_rule(const std::vector<Violation>& violations, Rule rule) {
    return static_cast<std::size_t>(
        std::count_if(violations.begin(), violations.end(), [&](const Violation& v) { return v.rule == rule; }));
}

// Three sections, three rooms, two instructors; solvable in many ways.
inline Problem small_problem() {
    Problem problem;
    problem.sections = {make_section("A-1", "I1", 30), make_section("B-1", "I2", 30),
                        make_section("L-1", "I1", 20, 1, true)};
    problem.rooms = {make_room("R1", 40), make_room("R2", 40), make_room("LAB", 25, true)};
    problem.instructors = {make_instructor("I1", 12), make_instructor("I2", 12)};
    return problem;
}

// Random but reproducible inputs for property-style tests. Some are unsolvable on purpose.
inline Problem random_problem(unsigned seed) {
    std::mt19937 rng(seed);
    auto pick = [&](int low, int high) { return std::uniform_int_distribution<int>(low, high)(rng); };

    Problem problem;
    const int rooms = pick(2, 6);
    for (int i = 0; i < rooms; ++i) {
        problem.rooms.push_back(make_room("R" + std::to_string(i), pick(15, 80), pick(0, 3) == 0));
    }
    const int instructors = pick(2, 6);
    for (int i = 0; i < instructors; ++i) {
        problem.instructors.push_back(make_instructor("I" + std::to_string(i), pick(3, 12)));
    }
    const int sections = pick(5, 30);
    for (int i = 0; i < sections; ++i) {
        problem.sections.push_back(make_section("S" + std::to_string(i) + "-1",
                                                "I" + std::to_string(pick(0, instructors - 1)), pick(5, 90),
                                                pick(1, 4), pick(0, 3) == 0));
    }
    const int students = pick(0, 40);
    for (int s = 0; s < students; ++s) {
        const int courses = pick(1, 4);
        for (int c = 0; c < courses; ++c) {
            problem.registrations.push_back(
                {"ST" + std::to_string(s), problem.sections[static_cast<std::size_t>(pick(0, sections - 1))].id, 0});
        }
    }
    return problem;
}

}  // namespace helpers
