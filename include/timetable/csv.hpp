#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "timetable/models.hpp"

namespace timetable {

enum class Severity { Error, Warning };

// A problem found in the input data, pointing at the file, line and field.
struct Issue {
    Severity severity = Severity::Error;
    std::string file;
    int line = 0;       // 0 when the issue is not about one line
    std::string field;  // empty when the issue is not about one field
    std::string message;
};

// "error: courses.csv line 4, field 'credits': expected a whole number, got 'abc'"
std::string to_string(const Issue& issue);
bool has_errors(const std::vector<Issue>& issues);

using Row = std::vector<std::string>;

struct CsvTable {
    Row header;  // lower-cased, trimmed column names
    int header_line = 0;
    std::vector<Row> rows;
    std::vector<int> lines;  // source line of each row
};

// Reads CSV with quoted fields, "" escapes, CRLF line ends and a UTF-8 BOM.
// Blank lines are skipped. Fields are trimmed.
CsvTable parse_csv(std::istream& in, const std::string& file, std::vector<Issue>& issues);

// Each loader checks columns, types and duplicate ids, reports problems to `issues`
// and returns only the rows that are valid.
std::vector<Section> load_sections(const CsvTable& table, const std::string& file, std::vector<Issue>& issues);
std::vector<Room> load_rooms(const CsvTable& table, const std::string& file, std::vector<Issue>& issues);
std::vector<Instructor> load_instructors(const CsvTable& table, const std::string& file, std::vector<Issue>& issues);
std::vector<Registration> load_registrations(const CsvTable& table, const std::string& file,
                                             std::vector<Issue>& issues);

enum class FileKind { Sections, Rooms, Instructors, Registrations, Unknown };

// Works out what a file holds from its header, so files can be given in any order.
FileKind detect_kind(const Row& header);
const char* kind_name(FileKind kind);

// File names used in cross-file messages.
struct SourceFiles {
    std::string sections = "courses.csv";
    std::string rooms = "rooms.csv";
    std::string instructors = "instructors.csv";
    std::string registrations = "registrations.csv";
};

// Checks that need more than one file: unknown ids (errors) and sections that can
// never be placed or instructors who are over-committed (warnings).
std::vector<Issue> check_problem(const Problem& problem, const SourceFiles& files = {});

struct LoadResult {
    Problem problem;
    SourceFiles files;
    std::vector<Issue> issues;
};

// Loads courses, rooms, instructors and (optionally) registrations from CSV files
// in any order, then runs check_problem if every file loaded cleanly.
LoadResult load_files(const std::vector<std::string>& paths);

}  // namespace timetable
