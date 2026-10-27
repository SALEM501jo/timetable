#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"
#include "timetable/csv.hpp"

using namespace timetable;
using namespace helpers;

namespace {

const std::string kSectionsHeader = "section_id,course_code,title,credits,instructor_id,enrolment,needs_lab\n";

std::vector<Section> sections_from(const std::string& text, std::vector<Issue>& issues) {
    return load_sections(table_from(text, issues), "courses.csv", issues);
}

}  // namespace

TEST_CASE("parse_csv handles quoted fields, escaped quotes and commas") {
    std::vector<Issue> issues;
    const CsvTable table = table_from("a,b,c\n1,\"x, y\",\"say \"\"hi\"\"\"\n", issues);
    REQUIRE(issues.empty());
    REQUIRE(table.rows.size() == 1);
    CHECK(table.rows[0] == Row{"1", "x, y", "say \"hi\""});
}

TEST_CASE("parse_csv skips blank lines and handles CRLF, BOM and line numbers") {
    std::vector<Issue> issues;
    const CsvTable table = table_from("\xEF\xBB\xBF" "A , B\r\n\r\n1,2\r\n   \r\n3,4\r\n", issues);
    REQUIRE(issues.empty());
    CHECK(table.header == Row{"a", "b"});
    REQUIRE(table.rows.size() == 2);
    CHECK(table.rows[1] == Row{"3", "4"});
    CHECK(table.lines == std::vector<int>{3, 5});
}

TEST_CASE("parse_csv allows a quoted field across lines and reports an unclosed quote") {
    std::vector<Issue> issues;
    const CsvTable ok = table_from("a,b\n\"two\nlines\",x\n", issues);
    REQUIRE(issues.empty());
    CHECK(ok.rows[0][0] == "two\nlines");

    table_from("a,b\n\"never closed,x\n", issues);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].line == 2);
    CHECK(issues[0].message.find("never closed") != std::string::npos);
}

TEST_CASE("a valid courses file loads every section") {
    std::vector<Issue> issues;
    const auto sections = sections_from(kSectionsHeader +
                                            "CS101-1,CS101,Intro to Programming,3,I01,35,false\n"
                                            "CS101L-1,CS101L,Programming Lab,1,I02,20,TRUE\n",
                                        issues);
    REQUIRE(issues.empty());
    REQUIRE(sections.size() == 2);
    CHECK(sections[0].id == "CS101-1");
    CHECK(sections[0].credits == 3);
    CHECK(sections[0].enrolment == 35);
    CHECK_FALSE(sections[0].needs_lab);
    CHECK(sections[1].needs_lab);
    CHECK(sections[1].source_line == 3);
}

TEST_CASE("columns may come in any order and extra columns are ignored") {
    std::vector<Issue> issues;
    const auto rooms = load_rooms(table_from("is_lab,notes,capacity,room_id\ntrue,new,25,LAB1\n", issues),
                                  "rooms.csv", issues);
    REQUIRE(issues.empty());
    REQUIRE(rooms.size() == 1);
    CHECK(rooms[0].id == "LAB1");
    CHECK(rooms[0].capacity == 25);
    CHECK(rooms[0].is_lab);
}

TEST_CASE("a missing column is reported by name") {
    std::vector<Issue> issues;
    const auto sections = sections_from("section_id,course_code,title,credits,instructor_id,needs_lab\n", issues);
    CHECK(sections.empty());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].field == "enrolment");
    CHECK(issues[0].line == 1);
}

TEST_CASE("a duplicate id is reported with both line numbers") {
    std::vector<Issue> issues;
    const auto sections = sections_from(kSectionsHeader +
                                            "CS101-1,CS101,Intro,3,I01,35,false\n"
                                            "CS101-1,CS101,Intro,3,I01,30,false\n",
                                        issues);
    CHECK(sections.size() == 1);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].line == 3);
    CHECK(issues[0].message.find("first defined on line 2") != std::string::npos);
}

TEST_CASE("a non-numeric value is reported with its line and field") {
    std::vector<Issue> issues;
    const auto sections = sections_from(kSectionsHeader + "CS101-1,CS101,Intro,three,I01,35,false\n", issues);
    CHECK(sections.empty());
    REQUIRE(issues.size() == 1);
    CHECK(to_string(issues[0]) ==
          "error: courses.csv line 2, field 'credits': expected a whole number, got 'three'");
}

TEST_CASE("negative and out-of-range numbers are rejected") {
    std::vector<Issue> issues;
    sections_from(kSectionsHeader + "A-1,A,A,3,I01,-5,false\nB-1,B,B,0,I01,10,false\n", issues);
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].field == "enrolment");
    CHECK(issues[1].field == "credits");
    CHECK(any_issue_mentions(issues, "must be at least 1"));
}

TEST_CASE("bad booleans, empty ids and wrong field counts are reported") {
    std::vector<Issue> issues;
    const auto sections = sections_from(kSectionsHeader +
                                            "A-1,A,A,3,I01,10,maybe\n"
                                            ",B,B,3,I01,10,false\n"
                                            "C-1,C,C,3,I01\n",
                                        issues);
    CHECK(sections.empty());
    REQUIRE(issues.size() == 3);
    CHECK(issues[0].message == "expected true or false, got 'maybe'");
    CHECK(issues[1].field == "section_id");
    CHECK(issues[2].message == "expected 7 fields, found 5");
}

TEST_CASE("instructors and registrations load; repeated registrations only warn") {
    std::vector<Issue> issues;
    const auto instructors = load_instructors(
        table_from("instructor_id,name,max_weekly_hours\nI01,\"Khalil, Ahmad\",12\n", issues), "i.csv", issues);
    REQUIRE(instructors.size() == 1);
    CHECK(instructors[0].name == "Khalil, Ahmad");

    const auto registrations = load_registrations(
        table_from("student_id,section_id\nS1,A-1\nS1,A-1\nS2,A-1\n", issues), "r.csv", issues);
    CHECK(registrations.size() == 2);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == Severity::Warning);
    CHECK_FALSE(has_errors(issues));
}

TEST_CASE("file kind is detected from the header") {
    CHECK(detect_kind({"section_id", "credits"}) == FileKind::Sections);
    CHECK(detect_kind({"room_id", "capacity", "is_lab"}) == FileKind::Rooms);
    CHECK(detect_kind({"instructor_id", "name", "max_weekly_hours"}) == FileKind::Instructors);
    CHECK(detect_kind({"student_id", "section_id"}) == FileKind::Registrations);
    CHECK(detect_kind({"foo", "bar"}) == FileKind::Unknown);
}

TEST_CASE("check_problem reports an unknown instructor as an error with its line") {
    Problem problem = small_problem();
    problem.sections[1].instructor_id = "I99";
    problem.sections[1].source_line = 3;
    const auto issues = check_problem(problem);
    REQUIRE(has_errors(issues));
    CHECK(any_issue_mentions(issues, "courses.csv line 3, field 'instructor_id': unknown instructor 'I99'"));
}

TEST_CASE("check_problem warns about sections no room can hold") {
    Problem problem = small_problem();
    problem.sections.push_back(make_section("BIG-1", "I2", 500));
    problem.sections.push_back(make_section("BIGLAB-1", "I2", 30, 1, true));
    const auto issues = check_problem(problem);
    CHECK_FALSE(has_errors(issues));
    CHECK(any_issue_mentions(issues, "BIG-1 has 500 students but the largest room holds 40"));
    CHECK(any_issue_mentions(issues, "BIGLAB-1 has 30 students but the largest lab room holds 25"));
}

TEST_CASE("check_problem warns about over-committed instructors and unknown registrations") {
    Problem problem = small_problem();
    problem.instructors[1].max_weekly_hours = 2;
    problem.registrations.push_back({"S1", "NOPE-1", 4});
    const auto issues = check_problem(problem);
    CHECK(any_issue_mentions(issues, "I2 is assigned 3h of sections but may teach at most 2h"));
    CHECK(any_issue_mentions(issues, "registrations.csv line 4, field 'section_id': unknown section 'NOPE-1'"));
}

TEST_CASE("load_files reads the sample data in any order") {
    const std::string dir = TIMETABLE_DATA_DIR;
    const LoadResult result = load_files({dir + "/sample_rooms.csv", dir + "/sample_registrations.csv",
                                          dir + "/sample_instructors.csv", dir + "/sample_courses.csv"});
    CHECK_FALSE(has_errors(result.issues));
    CHECK(result.problem.sections.size() == 21);
    CHECK(result.problem.rooms.size() == 8);
    CHECK(result.problem.instructors.size() == 8);
    CHECK(result.problem.registrations.size() == 47);
    // The sample data deliberately contains two impossible sections and one over-committed instructor.
    CHECK(result.issues.size() == 3);
}

TEST_CASE("load_files reports missing and unreadable files") {
    const LoadResult result = load_files({"does/not/exist.csv"});
    CHECK(any_issue_mentions(result.issues, "cannot open file"));
    CHECK(any_issue_mentions(result.issues, "no courses file given"));
    CHECK(any_issue_mentions(result.issues, "no rooms file given"));
    CHECK(any_issue_mentions(result.issues, "no instructors file given"));
}
