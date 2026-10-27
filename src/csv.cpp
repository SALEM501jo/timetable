#include "timetable/csv.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <istream>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

namespace timetable {

namespace {

std::string trim(const std::string& text) {
    const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    auto first = std::find_if_not(text.begin(), text.end(), is_space);
    auto last = std::find_if_not(text.rbegin(), text.rend(), is_space).base();
    return first < last ? std::string(first, last) : std::string();
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

using ColumnMap = std::unordered_map<std::string, std::size_t>;

std::optional<ColumnMap> map_columns(const CsvTable& table, const std::vector<std::string>& required,
                                     const std::string& file, std::vector<Issue>& issues) {
    if (table.header.empty()) {
        issues.push_back({Severity::Error, file, 0, "", "file is empty (expected a header row)"});
        return std::nullopt;
    }
    ColumnMap columns;
    for (std::size_t i = 0; i < table.header.size(); ++i) columns.emplace(table.header[i], i);

    bool complete = true;
    for (const auto& name : required) {
        if (columns.count(name) == 0) {
            issues.push_back({Severity::Error, file, table.header_line, name, "required column is missing"});
            complete = false;
        }
    }
    if (!complete) return std::nullopt;
    return columns;
}

// Reads typed fields from one row and records an issue for each bad field.
class RowReader {
public:
    RowReader(const ColumnMap& columns, const Row& row, std::size_t expected_fields, const std::string& file,
              int line, std::vector<Issue>& issues)
        : columns_(columns), row_(row), file_(file), line_(line), issues_(issues) {
        if (row.size() != expected_fields) {
            fail("", "expected " + std::to_string(expected_fields) + " fields, found " + std::to_string(row.size()));
            shape_ok_ = false;
        }
    }

    bool ok() const { return ok_; }

    std::string text(const std::string& column) {
        if (!shape_ok_) return {};
        const std::string& value = cell(column);
        if (value.empty()) fail(column, "must not be empty");
        return value;
    }

    int whole_number(const std::string& column, int min_value) {
        if (!shape_ok_) return 0;
        const std::string& value = cell(column);
        int result = 0;
        const char* first = value.data();
        const char* last = first + value.size();
        const auto [end, error] = std::from_chars(first, last, result);
        if (value.empty() || error != std::errc() || end != last) {
            fail(column, "expected a whole number, got '" + value + "'");
            return 0;
        }
        if (result < min_value) {
            fail(column, "must be at least " + std::to_string(min_value) + ", got " + value);
            return 0;
        }
        return result;
    }

    bool yes_no(const std::string& column) {
        if (!shape_ok_) return false;
        const std::string value = lower(cell(column));
        if (value == "true" || value == "yes" || value == "1") return true;
        if (value == "false" || value == "no" || value == "0") return false;
        fail(column, "expected true or false, got '" + cell(column) + "'");
        return false;
    }

private:
    const std::string& cell(const std::string& column) const { return row_[columns_.at(column)]; }

    void fail(const std::string& column, const std::string& message) {
        issues_.push_back({Severity::Error, file_, line_, column, message});
        ok_ = false;
    }

    const ColumnMap& columns_;
    const Row& row_;
    const std::string& file_;
    int line_;
    std::vector<Issue>& issues_;
    bool shape_ok_ = true;
    bool ok_ = true;
};

// Shared loop for every loader: check columns, read each row, keep the valid ones.
template <typename T, typename ReadRow>
std::vector<T> load_rows(const CsvTable& table, const std::vector<std::string>& required, const std::string& file,
                         std::vector<Issue>& issues, ReadRow read_row) {
    std::vector<T> items;
    const auto columns = map_columns(table, required, file, issues);
    if (!columns) return items;
    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        RowReader reader(*columns, table.rows[i], table.header.size(), file, table.lines[i], issues);
        T item = read_row(reader);
        if (!reader.ok()) continue;
        item.source_line = table.lines[i];
        items.push_back(std::move(item));
    }
    return items;
}

template <typename T>
void drop_duplicate_ids(std::vector<T>& items, const std::string& column, const std::string& file,
                        std::vector<Issue>& issues) {
    std::unordered_map<std::string, int> first_line;
    std::vector<T> unique;
    for (auto& item : items) {
        const auto [it, inserted] = first_line.emplace(item.id, item.source_line);
        if (inserted) {
            unique.push_back(std::move(item));
        } else {
            issues.push_back({Severity::Error, file, item.source_line, column,
                              "duplicate '" + item.id + "' (first defined on line " + std::to_string(it->second) +
                                  ")"});
        }
    }
    items = std::move(unique);
}

bool contains(const Row& header, const std::string& column) {
    return std::find(header.begin(), header.end(), column) != header.end();
}

}  // namespace

std::string to_string(const Issue& issue) {
    std::string out = issue.severity == Severity::Error ? "error: " : "warning: ";
    out += issue.file.empty() ? "input" : issue.file;
    if (issue.line > 0) out += " line " + std::to_string(issue.line);
    if (!issue.field.empty()) out += ", field '" + issue.field + "'";
    out += ": " + issue.message;
    return out;
}

bool has_errors(const std::vector<Issue>& issues) {
    return std::any_of(issues.begin(), issues.end(),
                       [](const Issue& issue) { return issue.severity == Severity::Error; });
}

CsvTable parse_csv(std::istream& in, const std::string& file, std::vector<Issue>& issues) {
    CsvTable table;
    std::string line;
    int line_no = 0;
    bool have_header = false;

    while (std::getline(in, line)) {
        ++line_no;
        if (line_no == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        const int record_line = line_no;
        Row row;
        std::string field;
        bool in_quotes = false;
        bool broken = false;

        // A quoted field may continue onto the next physical line.
        for (;;) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            for (std::size_t i = 0; i < line.size(); ++i) {
                const char ch = line[i];
                if (in_quotes) {
                    if (ch != '"') {
                        field += ch;
                    } else if (i + 1 < line.size() && line[i + 1] == '"') {
                        field += '"';
                        ++i;
                    } else {
                        in_quotes = false;
                    }
                } else if (ch == '"') {
                    in_quotes = true;
                } else if (ch == ',') {
                    row.push_back(trim(field));
                    field.clear();
                } else {
                    field += ch;
                }
            }
            if (!in_quotes) break;
            if (!std::getline(in, line)) {
                issues.push_back({Severity::Error, file, record_line, "", "quoted field is never closed"});
                broken = true;
                break;
            }
            ++line_no;
            field += '\n';
        }
        if (broken) continue;
        row.push_back(trim(field));

        if (row.size() == 1 && row[0].empty()) continue;  // blank line
        if (!have_header) {
            for (auto& name : row) name = lower(name);
            table.header = std::move(row);
            table.header_line = record_line;
            have_header = true;
        } else {
            table.rows.push_back(std::move(row));
            table.lines.push_back(record_line);
        }
    }
    return table;
}

std::vector<Section> load_sections(const CsvTable& table, const std::string& file, std::vector<Issue>& issues) {
    auto sections = load_rows<Section>(
        table, {"section_id", "course_code", "title", "credits", "instructor_id", "enrolment", "needs_lab"}, file,
        issues, [](RowReader& row) {
            Section section;
            section.id = row.text("section_id");
            section.course_code = row.text("course_code");
            section.title = row.text("title");
            section.credits = row.whole_number("credits", 1);
            section.instructor_id = row.text("instructor_id");
            section.enrolment = row.whole_number("enrolment", 0);
            section.needs_lab = row.yes_no("needs_lab");
            return section;
        });
    drop_duplicate_ids(sections, "section_id", file, issues);
    return sections;
}

std::vector<Room> load_rooms(const CsvTable& table, const std::string& file, std::vector<Issue>& issues) {
    auto rooms = load_rows<Room>(table, {"room_id", "capacity", "is_lab"}, file, issues, [](RowReader& row) {
        Room room;
        room.id = row.text("room_id");
        room.capacity = row.whole_number("capacity", 1);
        room.is_lab = row.yes_no("is_lab");
        return room;
    });
    drop_duplicate_ids(rooms, "room_id", file, issues);
    return rooms;
}

std::vector<Instructor> load_instructors(const CsvTable& table, const std::string& file, std::vector<Issue>& issues) {
    auto instructors = load_rows<Instructor>(table, {"instructor_id", "name", "max_weekly_hours"}, file, issues,
                                             [](RowReader& row) {
                                                 Instructor instructor;
                                                 instructor.id = row.text("instructor_id");
                                                 instructor.name = row.text("name");
                                                 instructor.max_weekly_hours = row.whole_number("max_weekly_hours", 0);
                                                 return instructor;
                                             });
    drop_duplicate_ids(instructors, "instructor_id", file, issues);
    return instructors;
}

std::vector<Registration> load_registrations(const CsvTable& table, const std::string& file,
                                             std::vector<Issue>& issues) {
    auto registrations =
        load_rows<Registration>(table, {"student_id", "section_id"}, file, issues, [](RowReader& row) {
            Registration registration;
            registration.student_id = row.text("student_id");
            registration.section_id = row.text("section_id");
            return registration;
        });

    // A repeated registration is harmless, so warn and keep the first.
    std::map<std::pair<std::string, std::string>, int> first_line;
    std::vector<Registration> unique;
    for (auto& registration : registrations) {
        const auto [it, inserted] =
            first_line.emplace(std::make_pair(registration.student_id, registration.section_id),
                               registration.source_line);
        if (inserted) {
            unique.push_back(std::move(registration));
        } else {
            issues.push_back({Severity::Warning, file, registration.source_line, "section_id",
                              "student " + registration.student_id + " is already registered in " +
                                  registration.section_id + " (line " + std::to_string(it->second) +
                                  "); duplicate ignored"});
        }
    }
    return unique;
}

FileKind detect_kind(const Row& header) {
    if (contains(header, "student_id")) return FileKind::Registrations;
    if (contains(header, "credits")) return FileKind::Sections;
    if (contains(header, "capacity")) return FileKind::Rooms;
    if (contains(header, "max_weekly_hours")) return FileKind::Instructors;
    return FileKind::Unknown;
}

const char* kind_name(FileKind kind) {
    switch (kind) {
        case FileKind::Sections: return "courses";
        case FileKind::Rooms: return "rooms";
        case FileKind::Instructors: return "instructors";
        case FileKind::Registrations: return "registrations";
        case FileKind::Unknown: break;
    }
    return "unknown";
}

std::vector<Issue> check_problem(const Problem& problem, const SourceFiles& files) {
    std::vector<Issue> issues;
    std::map<std::string, int> required_minutes;  // ordered, so warnings come out in id order

    for (const auto& section : problem.sections) {
        if (problem.find_instructor(section.instructor_id) == nullptr) {
            issues.push_back({Severity::Error, files.sections, section.source_line, "instructor_id",
                              "unknown instructor '" + section.instructor_id + "' (not in " + files.instructors +
                                  ")"});
        } else {
            required_minutes[section.instructor_id] += section.credits * problem.config.minutes_per_credit;
        }

        if (allowed_patterns(section, problem.config).empty()) {
            issues.push_back({Severity::Warning, files.sections, section.source_line, "credits",
                              "no allowed meeting pattern fits " + std::to_string(section.credits) +
                                  " credit hours; " + section.id + " cannot be placed"});
        }

        int largest = 0;
        for (const auto& room : problem.rooms) {
            if (room.is_lab || !section.needs_lab) largest = std::max(largest, room.capacity);
        }
        if (largest < section.enrolment) {
            const std::string kind = section.needs_lab ? "lab room" : "room";
            const std::string detail =
                largest == 0 ? "there is no " + kind : "the largest " + kind + " holds " + std::to_string(largest);
            issues.push_back({Severity::Warning, files.sections, section.source_line, "enrolment",
                              section.id + " has " + std::to_string(section.enrolment) + " students but " + detail +
                                  "; it cannot be placed"});
        }
    }

    for (const auto& [id, minutes] : required_minutes) {
        const Instructor* instructor = problem.find_instructor(id);
        if (minutes > instructor->max_weekly_hours * 60) {
            issues.push_back({Severity::Warning, files.instructors, instructor->source_line, "max_weekly_hours",
                              id + " is assigned " + format_hours(minutes) + " of sections but may teach at most " +
                                  std::to_string(instructor->max_weekly_hours) + "h; some will be unplaced"});
        }
    }

    for (const auto& registration : problem.registrations) {
        if (problem.find_section(registration.section_id) == nullptr) {
            issues.push_back({Severity::Error, files.registrations, registration.source_line, "section_id",
                              "unknown section '" + registration.section_id + "' (not in " + files.sections + ")"});
        }
    }
    return issues;
}

LoadResult load_files(const std::vector<std::string>& paths) {
    LoadResult result;
    auto& issues = result.issues;
    std::map<FileKind, std::string> loaded_from;

    for (const auto& path : paths) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            issues.push_back({Severity::Error, path, 0, "", "cannot open file"});
            continue;
        }
        const CsvTable table = parse_csv(in, path, issues);
        if (table.header.empty()) {
            issues.push_back({Severity::Error, path, 0, "", "file is empty (expected a header row)"});
            continue;
        }
        const FileKind kind = detect_kind(table.header);
        if (kind == FileKind::Unknown) {
            issues.push_back({Severity::Error, path, table.header_line, "",
                              "unrecognised file: the header needs a credits, capacity, max_weekly_hours or "
                              "student_id column"});
            continue;
        }
        const auto [it, first] = loaded_from.emplace(kind, path);
        if (!first) {
            issues.push_back({Severity::Error, path, 0, "",
                              std::string("more than one ") + kind_name(kind) + " file (also " + it->second + ")"});
            continue;
        }

        switch (kind) {
            case FileKind::Sections:
                result.problem.sections = load_sections(table, path, issues);
                result.files.sections = path;
                break;
            case FileKind::Rooms:
                result.problem.rooms = load_rooms(table, path, issues);
                result.files.rooms = path;
                break;
            case FileKind::Instructors:
                result.problem.instructors = load_instructors(table, path, issues);
                result.files.instructors = path;
                break;
            case FileKind::Registrations:
                result.problem.registrations = load_registrations(table, path, issues);
                result.files.registrations = path;
                break;
            case FileKind::Unknown:
                break;
        }
    }

    for (FileKind kind : {FileKind::Sections, FileKind::Rooms, FileKind::Instructors}) {
        if (loaded_from.count(kind) == 0) {
            issues.push_back({Severity::Error, "", 0, "", std::string("no ") + kind_name(kind) + " file given"});
        }
    }

    if (!has_errors(issues)) {
        auto cross_file = check_problem(result.problem, result.files);
        issues.insert(issues.end(), cross_file.begin(), cross_file.end());
    }
    return result;
}

}  // namespace timetable
