// timetable_cli: build a conflict-free timetable from CSV files and print reports.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "timetable/csv.hpp"
#include "timetable/report.hpp"
#include "timetable/scheduler.hpp"
#include "timetable/validator.hpp"

namespace fs = std::filesystem;
using namespace timetable;

namespace {

constexpr const char* kUsage = R"(Usage: timetable_cli [options] <csv file or folder>...

Builds a timetable from courses, rooms, instructors and (optional)
registrations CSV files. Files are recognised by their header, so they
can be given in any order. A folder means every .csv file in it.

Options:
  --order static|constrained  section ordering (default: static)
  --compare                   run both orderings and compare them
  --out FILE                  also write the timetable to FILE as CSV
  -h, --help                  show this help

Exit codes: 0 done, 2 bad input or usage, 3 validator found violations.
)";

struct Options {
    std::vector<std::string> inputs;
    Ordering ordering = Ordering::Static;
    bool compare = false;
    bool help = false;
    std::string out_path;
};

// Returns nullopt (after printing why) when the arguments are wrong.
std::optional<Options> parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            options.help = true;
            return options;
        }
        if (arg == "--compare") {
            options.compare = true;
        } else if (arg == "--order" || arg == "--out") {
            if (i + 1 >= argc) {
                std::cerr << arg << " needs a value\n";
                return std::nullopt;
            }
            const std::string value = argv[++i];
            if (arg == "--out") {
                options.out_path = value;
            } else if (value == "static") {
                options.ordering = Ordering::Static;
            } else if (value == "constrained") {
                options.ordering = Ordering::MostConstrained;
            } else {
                std::cerr << "unknown ordering '" << value << "' (use static or constrained)\n";
                return std::nullopt;
            }
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "unknown option " << arg << "\n\n" << kUsage;
            return std::nullopt;
        } else {
            options.inputs.push_back(arg);
        }
    }
    if (options.inputs.empty()) {
        std::cerr << kUsage;
        return std::nullopt;
    }
    return options;
}

// Replaces each folder with the .csv files inside it, in name order.
std::vector<std::string> expand_inputs(const std::vector<std::string>& inputs) {
    std::vector<std::string> files;
    for (const auto& input : inputs) {
        std::error_code error;
        if (!fs::is_directory(input, error)) {
            files.push_back(input);
            continue;
        }
        std::vector<std::string> found;
        for (const auto& entry : fs::directory_iterator(input, error)) {
            if (entry.is_regular_file() && entry.path().extension() == ".csv") found.push_back(entry.path().string());
        }
        std::sort(found.begin(), found.end());
        files.insert(files.end(), found.begin(), found.end());
    }
    return files;
}

const char* ordering_name(Ordering ordering) {
    return ordering == Ordering::Static ? "static" : "constrained";
}

}  // namespace

int main(int argc, char** argv) {
    const auto options = parse_args(argc, argv);
    if (!options) return 2;
    if (options->help) {
        std::cout << kUsage;
        return 0;
    }

    const LoadResult loaded = load_files(expand_inputs(options->inputs));
    for (const auto& issue : loaded.issues) std::cerr << to_string(issue) << '\n';
    if (has_errors(loaded.issues)) {
        std::cerr << "Stopped: fix the errors above and run again.\n";
        return 2;
    }
    const Problem& problem = loaded.problem;
    const std::size_t total = problem.sections.size();

    if (options->compare) {
        bool all_valid = true;
        std::cout << "Ordering     Placed  Unplaced  Violations\n";
        for (Ordering ordering : {Ordering::Static, Ordering::MostConstrained}) {
            const ScheduleResult result = schedule(problem, {ordering});
            const auto violations = validate(problem, result.timetable);
            all_valid = all_valid && violations.empty();
            std::cout << ordering_name(ordering) << std::string(13 - std::string(ordering_name(ordering)).size(), ' ')
                      << result.timetable.assignments.size() << '/' << total << "   " << result.unplaced.size()
                      << "         " << violations.size() << '\n';
        }
        return all_valid ? 0 : 3;
    }

    const ScheduleResult result = schedule(problem, {options->ordering});
    const auto violations = validate(problem, result.timetable);

    std::cout << "\nTIMETABLE (" << result.timetable.assignments.size() << " of " << total
              << " sections placed, ordering: " << ordering_name(options->ordering) << ")\n\n";
    print_timetable(std::cout, problem, result.timetable);
    std::cout << "\nUNPLACED SECTIONS\n\n";
    print_unplaced(std::cout, problem, result.unplaced);
    std::cout << "\nROOM UTILISATION\n\n";
    print_room_usage(std::cout, room_usage(problem, result.timetable));
    std::cout << "\nINSTRUCTOR LOAD\n\n";
    print_instructor_loads(std::cout, instructor_loads(problem, result.timetable));
    std::cout << '\n';
    print_violations(std::cout, violations);

    if (!options->out_path.empty()) {
        std::ofstream out(options->out_path, std::ios::binary);
        if (!out) {
            std::cerr << "cannot write " << options->out_path << '\n';
            return 2;
        }
        write_timetable_csv(out, problem, result.timetable);
        std::cout << "Wrote " << options->out_path << '\n';
    }
    return violations.empty() ? 0 : 3;
}
