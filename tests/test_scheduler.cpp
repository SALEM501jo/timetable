#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <set>
#include <sstream>

#include "helpers.hpp"
#include "timetable/report.hpp"
#include "timetable/scheduler.hpp"
#include "timetable/validator.hpp"

using namespace timetable;
using namespace helpers;

namespace {

std::string reason_for(const ScheduleResult& result, const std::string& section_id) {
    for (const auto& u : result.unplaced) {
        if (u.section_id == section_id) return u.reason;
    }
    return "";
}

// One day (Sun) of two 30-minute slots: room for exactly one 1-credit lab per resource.
Problem tiny_day_problem() {
    Problem problem;
    problem.config.day_names = {"Sun"};
    problem.config.day_end_minutes = 9 * 60;
    return problem;
}

}  // namespace

TEST_CASE("a small solvable problem is fully placed and passes the validator") {
    const Problem problem = small_problem();
    const ScheduleResult result = schedule(problem);
    CHECK(result.unplaced.empty());
    CHECK(result.timetable.assignments.size() == 3);
    CHECK(validate(problem, result.timetable).empty());
}

TEST_CASE("lab sections go to lab rooms and lectures prefer ordinary rooms") {
    const ScheduleResult result = schedule(small_problem());
    REQUIRE(result.timetable.find("L-1") != nullptr);
    CHECK(result.timetable.find("L-1")->room_id == "LAB");
    CHECK(result.timetable.find("A-1")->room_id != "LAB");
}

TEST_CASE("a section too big for every room is unplaced with the reason") {
    Problem problem = small_problem();
    problem.sections.push_back(make_section("BIG-1", "I2", 100));
    problem.sections.push_back(make_section("BIGLAB-1", "I2", 30, 1, true));
    const ScheduleResult result = schedule(problem);
    CHECK(reason_for(result, "BIG-1") == "no room holds 100 students (largest holds 40)");
    CHECK(reason_for(result, "BIGLAB-1") == "no lab room holds 30 students (largest holds 25)");
    CHECK(result.timetable.assignments.size() == 3);
    CHECK(validate(problem, result.timetable).empty());
}

TEST_CASE("an instructor's weekly limit forces a section to be unplaced") {
    Problem problem = small_problem();
    problem.instructors[1].max_weekly_hours = 5;
    problem.sections.push_back(make_section("C-1", "I2", 30));  // I2 now needs 6h
    const ScheduleResult result = schedule(problem);
    REQUIRE(result.unplaced.size() == 1);
    // Equal enrolment, so the static order breaks the tie by id: B-1 goes first.
    CHECK(result.unplaced[0].section_id == "C-1");
    CHECK(result.unplaced[0].reason == "instructor I2 would teach 6h a week, over the maximum of 5h");
    CHECK(validate(problem, result.timetable).empty());
}

TEST_CASE("an unknown instructor or impossible credits are explained") {
    Problem problem = small_problem();
    problem.sections.push_back(make_section("X-1", "I99", 10));
    problem.sections.push_back(make_section("Y-1", "I1", 10, 20));  // 20h a week cannot fit in a day
    const ScheduleResult result = schedule(problem);
    CHECK(reason_for(result, "X-1") == "instructor 'I99' is not in the instructors file");
    CHECK(reason_for(result, "Y-1") == "no allowed meeting pattern fits 20 credit hours");
}

TEST_CASE("a student in two sections gets them at different times") {
    Problem problem = tiny_day_problem();
    problem.config.day_end_minutes = 9 * 60;  // two slots
    problem.sections = {make_section("L1-1", "I1", 10, 1, true), make_section("L2-1", "I2", 10, 1, true)};
    problem.rooms = {make_room("LAB1", 20, true), make_room("LAB2", 20, true)};
    problem.instructors = {make_instructor("I1"), make_instructor("I2")};

    // Without a shared student both labs can start at 08:00; with one, the second must wait.
    CHECK(schedule(problem).timetable.assignments[1].start_slot == 0);

    problem.config.day_end_minutes = 10 * 60;  // four slots, room for two 1h meetings
    problem.registrations = {{"S1", "L1-1", 0}, {"S1", "L2-1", 0}};
    const ScheduleResult result = schedule(problem);
    REQUIRE(result.unplaced.empty());
    CHECK(result.timetable.assignments[0].start_slot != result.timetable.assignments[1].start_slot);
    CHECK(validate(problem, result.timetable).empty());
}

TEST_CASE("a student clash that cannot be avoided is explained") {
    Problem problem = tiny_day_problem();
    problem.config.day_end_minutes = 9 * 60;
    problem.sections = {make_section("L1-1", "I1", 10, 1, true), make_section("L2-1", "I2", 10, 1, true)};
    problem.rooms = {make_room("LAB1", 20, true), make_room("LAB2", 20, true)};
    problem.instructors = {make_instructor("I1"), make_instructor("I2")};
    problem.registrations = {{"S1", "L1-1", 0}, {"S1", "L2-1", 0}};
    const ScheduleResult result = schedule(problem);
    CHECK(reason_for(result, "L2-1") == "no time when instructor I2 and all 1 registered students are free");
}

TEST_CASE("full rooms and busy instructors are explained") {
    Problem problem = tiny_day_problem();
    problem.config.day_end_minutes = 9 * 60;
    problem.rooms = {make_room("LAB1", 20, true)};
    problem.instructors = {make_instructor("I1"), make_instructor("I2")};

    SECTION("one room, two instructors") {
        problem.sections = {make_section("L1-1", "I1", 10, 1, true), make_section("L2-1", "I2", 10, 1, true)};
        CHECK(reason_for(schedule(problem), "L2-1") ==
              "all suitable rooms (1) are booked whenever the instructor is free");
    }
    SECTION("two rooms, one instructor") {
        problem.rooms.push_back(make_room("LAB2", 20, true));
        problem.sections = {make_section("L1-1", "I1", 10, 1, true), make_section("L2-1", "I1", 10, 1, true)};
        CHECK(reason_for(schedule(problem), "L2-1") ==
              "instructor I1 has no free time for any allowed meeting pattern");
    }
}

TEST_CASE("the scheduler is deterministic") {
    const Problem problem = random_problem(7);
    for (Ordering ordering : {Ordering::Static, Ordering::MostConstrained}) {
        const ScheduleResult first = schedule(problem, {ordering});
        const ScheduleResult second = schedule(problem, {ordering});
        std::ostringstream a;
        std::ostringstream b;
        write_timetable_csv(a, problem, first.timetable);
        write_timetable_csv(b, problem, second.timetable);
        CHECK(a.str() == b.str());
        REQUIRE(first.unplaced.size() == second.unplaced.size());
        for (std::size_t i = 0; i < first.unplaced.size(); ++i) {
            CHECK(first.unplaced[i].section_id == second.unplaced[i].section_id);
            CHECK(first.unplaced[i].reason == second.unplaced[i].reason);
        }
    }
}

TEST_CASE("property: scheduler output always passes the validator") {
    const unsigned seed = GENERATE(range(1u, 101u));
    const Problem problem = random_problem(seed);
    for (Ordering ordering : {Ordering::Static, Ordering::MostConstrained}) {
        INFO("seed " << seed << ", ordering " << (ordering == Ordering::Static ? "static" : "constrained"));
        const ScheduleResult result = schedule(problem, {ordering});

        const auto violations = validate(problem, result.timetable);
        for (const auto& v : violations) UNSCOPED_INFO(v.message);
        CHECK(violations.empty());

        // Every section is either placed or unplaced, exactly once.
        std::multiset<std::string> seen;
        for (const auto& a : result.timetable.assignments) seen.insert(a.section_id);
        for (const auto& u : result.unplaced) {
            seen.insert(u.section_id);
            CHECK_FALSE(u.reason.empty());
        }
        CHECK(seen.size() == problem.sections.size());
        for (const auto& section : problem.sections) CHECK(seen.count(section.id) == 1);
    }
}
