#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "helpers.hpp"
#include "timetable/report.hpp"

using namespace timetable;
using namespace helpers;

namespace {

Timetable sample_timetable() {
    return Timetable{{
        make_assignment("A-1", "R1", {0, 2, 4}, 0, 2),
        make_assignment("B-1", "R2", {1, 3}, 2, 3),
        make_assignment("L-1", "LAB", {1}, 0, 2),
    }};
}

}  // namespace

TEST_CASE("room usage counts booked slots and seat fill") {
    const auto usage = room_usage(small_problem(), sample_timetable());
    REQUIRE(usage.size() == 3);
    const RoomUsage& r1 = usage[1];  // sorted by id: LAB, R1, R2
    CHECK(r1.room_id == "R1");
    CHECK(r1.sections == 1);
    CHECK(r1.used_slots == 6);
    CHECK(r1.total_slots == 5 * 18);
    CHECK(r1.utilisation == Catch::Approx(6.0 / 90.0));
    CHECK(r1.seat_fill == Catch::Approx(30.0 / 40.0));
    CHECK(usage[0].room_id == "LAB");
    CHECK(usage[0].seat_fill == Catch::Approx(20.0 / 25.0));
}

TEST_CASE("instructor load sums weekly minutes") {
    const auto loads = instructor_loads(small_problem(), sample_timetable());
    REQUIRE(loads.size() == 2);
    CHECK(loads[0].instructor_id == "I1");
    CHECK(loads[0].sections == 2);
    CHECK(loads[0].minutes == 180 + 60);
    CHECK(loads[0].max_minutes == 12 * 60);
}

TEST_CASE("csv_field quotes only when needed") {
    CHECK(csv_field("plain") == "plain");
    CHECK(csv_field("a,b") == "\"a,b\"");
    CHECK(csv_field("say \"hi\"") == "\"say \"\"hi\"\"\"");
}

TEST_CASE("timetable CSV export has a header and rows in week order") {
    Problem problem = small_problem();
    problem.sections[0].title = "Intro, Part 1";
    std::ostringstream out;
    write_timetable_csv(out, problem, sample_timetable());
    const std::string csv = out.str();
    CHECK(csv.rfind("section_id,course_code,title,instructor_id,instructor_name,room_id,days,start,end,", 0) == 0);
    CHECK(csv.find("A-1,A,\"Intro, Part 1\",I1,Instructor I1,R1,Sun/Tue/Thu,08:00,09:00,30,40\r\n") !=
          std::string::npos);
    // Sun lecture first, then the two Monday sections by start time.
    CHECK(csv.find("A-1,") < csv.find("L-1,"));
    CHECK(csv.find("L-1,") < csv.find("B-1,"));
}

TEST_CASE("time helpers format days, times and hours") {
    const TimeConfig config;
    CHECK(config.slots_per_day() == 18);
    CHECK(config.slot_time(0) == "08:00");
    CHECK(config.slot_time(3) == "09:30");
    CHECK(format_days({0, 2, 4}, config) == "Sun/Tue/Thu");
    CHECK(format_hours(180) == "3h");
    CHECK(format_hours(90) == "1.5h");
}

TEST_CASE("allowed patterns follow the credit rules") {
    const TimeConfig config;
    const auto three = allowed_patterns(make_section("A-1", "I1", 10, 3), config);
    REQUIRE(three.size() == 2);
    CHECK(three[0].name == "Sun/Tue/Thu");
    CHECK(three[0].length_slots == 2);  // 1h each
    CHECK(three[1].name == "Mon/Wed");
    CHECK(three[1].length_slots == 3);  // 1.5h each

    const auto two = allowed_patterns(make_section("B-1", "I1", 10, 2), config);
    REQUIRE(two.size() == 1);  // 40 min three times a week does not fit 30-min slots
    CHECK(two[0].name == "Mon/Wed");

    const auto lab = allowed_patterns(make_section("L-1", "I1", 10, 1, true), config);
    CHECK(lab.size() == 5);  // once a week, any day
}
