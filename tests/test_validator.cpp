#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"
#include "timetable/validator.hpp"

using namespace timetable;
using namespace helpers;

namespace {

// Day indexes with the default config (Sun..Thu) and 30-minute slots from 08:00.
const std::vector<int> kSunTueThu{0, 2, 4};
const std::vector<int> kMonWed{1, 3};

// A hand-built valid timetable for small_problem().
Timetable clean_timetable() {
    return Timetable{{
        make_assignment("A-1", "R1", kSunTueThu, 0, 2),  // 08:00-09:00
        make_assignment("B-1", "R2", kSunTueThu, 0, 2),
        make_assignment("L-1", "LAB", {1}, 0, 2),  // Mon 08:00-09:00
    }};
}

}  // namespace

TEST_CASE("a clean timetable has no violations") {
    CHECK(validate(small_problem(), clean_timetable()).empty());
}

TEST_CASE("back-to-back sections in one room do not clash") {
    Timetable timetable = clean_timetable();
    timetable.assignments[1] = make_assignment("B-1", "R1", kSunTueThu, 2, 2);  // starts when A-1 ends
    CHECK(validate(small_problem(), timetable).empty());
}

TEST_CASE("rule 1: two sections in one room at the same time") {
    Timetable timetable = clean_timetable();
    timetable.assignments[1] = make_assignment("B-1", "R1", kSunTueThu, 1, 2);  // overlaps A-1 by 30 min
    const auto violations = validate(small_problem(), timetable);
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule == Rule::RoomClash);
    CHECK(violations[0].message == "room R1 is double-booked at Sun 08:30: A-1 and B-1");
    CHECK(violations[0].section_ids == std::vector<std::string>{"A-1", "B-1"});
}

TEST_CASE("rule 2: an instructor teaching two sections at once") {
    Timetable timetable = clean_timetable();
    timetable.assignments[2] = make_assignment("L-1", "LAB", {2}, 1, 2);  // Tue, overlaps A-1 (both I1)
    const auto violations = validate(small_problem(), timetable);
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule == Rule::InstructorClash);
    CHECK(violations[0].message.find("instructor I1 teaches A-1 and L-1") != std::string::npos);
}

TEST_CASE("rule 3: a room smaller than the enrolment") {
    Problem problem = small_problem();
    problem.rooms[0].capacity = 20;
    const auto violations = validate(problem, clean_timetable());
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule == Rule::RoomCapacity);
    CHECK(violations[0].message == "A-1 has 30 students but room R1 holds 20");
}

TEST_CASE("rule 4: a lab section outside a lab room") {
    Timetable timetable = clean_timetable();
    timetable.assignments[2].room_id = "R2";  // Mon, so no clash with B-1
    const auto violations = validate(small_problem(), timetable);
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule == Rule::LabRoom);
}

TEST_CASE("rule 5: days that are not an allowed pattern") {
    Timetable timetable = clean_timetable();
    timetable.assignments[0].days = {0, 1, 2};
    const auto violations = validate(small_problem(), timetable);
    REQUIRE(count_rule(violations, Rule::AllowedTime) == 1);
    CHECK(violations[0].message.find("Sun/Mon/Tue 08:00-09:00, which is not allowed for a 3-credit lecture") !=
          std::string::npos);
}

TEST_CASE("rule 5: the wrong meeting length or a time outside the day") {
    Timetable too_long = clean_timetable();
    too_long.assignments[0].length_slots = 3;
    CHECK(count_rule(validate(small_problem(), too_long), Rule::AllowedTime) == 1);

    Timetable too_late = clean_timetable();
    too_late.assignments[0].start_slot = 17;  // 16:30-17:30, past the 17:00 end of day
    CHECK(count_rule(validate(small_problem(), too_late), Rule::AllowedTime) == 1);

    Timetable mon_wed = clean_timetable();
    mon_wed.assignments[0] = make_assignment("A-1", "R1", kMonWed, 2, 3);  // the other allowed pattern, after L-1
    CHECK(validate(small_problem(), mon_wed).empty());
}

TEST_CASE("rule 6: an instructor over the weekly maximum") {
    Problem problem = small_problem();
    problem.instructors[1].max_weekly_hours = 2;
    const auto violations = validate(problem, clean_timetable());
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule == Rule::InstructorLoad);
    CHECK(violations[0].message == "instructor I2 teaches 3h a week, over the maximum of 2h");
}

TEST_CASE("rule 7: a student registered in two sections at the same time") {
    Problem problem = small_problem();
    problem.registrations = {{"S1", "A-1", 0}, {"S1", "B-1", 0}, {"S1", "L-1", 0}};
    const auto violations = validate(problem, clean_timetable());
    REQUIRE(violations.size() == 1);
    CHECK(violations[0].rule == Rule::StudentClash);
    CHECK(violations[0].message == "student S1 has A-1 and B-1 at the same time (Sun 08:00)");
}

TEST_CASE("unknown rooms, unknown sections and double assignments are reported") {
    Timetable timetable = clean_timetable();
    timetable.assignments[1].room_id = "R9";
    timetable.assignments.push_back(make_assignment("A-1", "R2", kMonWed, 0, 3));
    timetable.assignments.push_back(make_assignment("GHOST-1", "R2", kMonWed, 0, 3));
    const auto violations = validate(small_problem(), timetable);
    CHECK(count_rule(violations, Rule::BadReference) == 3);
    CHECK(violations.size() == 3);
}

TEST_CASE("unplaced sections are not violations") {
    Timetable timetable = clean_timetable();
    timetable.assignments.pop_back();
    CHECK(validate(small_problem(), timetable).empty());
}
