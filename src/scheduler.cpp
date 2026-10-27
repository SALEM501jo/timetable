#include "timetable/scheduler.hpp"

#include <algorithm>
#include <climits>
#include <numeric>
#include <optional>
#include <unordered_map>

namespace timetable {

namespace {

// Busy/free cells for one resource (a room, an instructor or a student) over the week.
class Grid {
public:
    Grid(int days, int slots)
        : slots_(slots), busy_(static_cast<std::size_t>(days) * static_cast<std::size_t>(slots), false) {}

    bool is_free(const std::vector<int>& days, int start, int length) const {
        for (int day : days) {
            for (int slot = start; slot < start + length; ++slot) {
                if (busy_[index(day, slot)]) return false;
            }
        }
        return true;
    }

    void book(const std::vector<int>& days, int start, int length) {
        for (int day : days) {
            for (int slot = start; slot < start + length; ++slot) busy_[index(day, slot)] = true;
        }
    }

private:
    std::size_t index(int day, int slot) const { return static_cast<std::size_t>(day * slots_ + slot); }

    int slots_;
    std::vector<bool> busy_;
};

// A section plus everything about it that does not change while scheduling.
struct Job {
    const Section* section = nullptr;
    const Instructor* instructor = nullptr;  // null when the instructor id is unknown
    std::vector<MeetingPattern> patterns;
    std::vector<std::size_t> rooms;  // rooms meeting the capacity and lab rules, best fit first
    std::vector<std::string> students;
    int weekly_minutes = 0;
};

struct Option {
    const MeetingPattern* pattern;
    int start;
    std::size_t room;
};

// Static order: labs first (few lab rooms), then large sections (few big rooms).
bool goes_first(const Job& a, const Job& b) {
    if (a.section->needs_lab != b.section->needs_lab) return a.section->needs_lab;
    if (a.section->enrolment != b.section->enrolment) return a.section->enrolment > b.section->enrolment;
    return a.section->id < b.section->id;
}

class Scheduler {
public:
    explicit Scheduler(const Problem& problem);
    ScheduleResult run(Ordering ordering);

private:
    // Calls visit(option) for each free option, in a fixed order, until visit returns true.
    template <typename Visit>
    void for_each_option(const Job& job, Visit visit) const;
    std::optional<Option> first_option(const Job& job) const;
    int count_options(const Job& job) const;
    bool within_load(const Job& job) const;
    bool people_free(const Job& job, const MeetingPattern& pattern, int start) const;
    std::string why_unplaced(const Job& job) const;
    Assignment book(const Job& job, const Option& option);

    const Problem& problem_;
    std::vector<Job> jobs_;
    std::vector<Grid> room_busy_;
    std::unordered_map<std::string, Grid> instructor_busy_;
    std::unordered_map<std::string, Grid> student_busy_;
    std::unordered_map<std::string, int> instructor_minutes_;
};

Scheduler::Scheduler(const Problem& problem) : problem_(problem) {
    const TimeConfig& config = problem.config;
    const int days = config.day_count();
    const int slots = config.slots_per_day();

    room_busy_.assign(problem.rooms.size(), Grid(days, slots));
    for (const auto& instructor : problem.instructors) {
        instructor_busy_.emplace(instructor.id, Grid(days, slots));
        instructor_minutes_.emplace(instructor.id, 0);
    }

    // Best fit: ordinary rooms before labs, then smallest first, so big rooms and
    // labs stay free for the sections that need them.
    std::vector<std::size_t> room_order(problem.rooms.size());
    std::iota(room_order.begin(), room_order.end(), std::size_t{0});
    std::sort(room_order.begin(), room_order.end(), [&](std::size_t a, std::size_t b) {
        const Room& ra = problem.rooms[a];
        const Room& rb = problem.rooms[b];
        if (ra.is_lab != rb.is_lab) return !ra.is_lab;
        if (ra.capacity != rb.capacity) return ra.capacity < rb.capacity;
        return ra.id < rb.id;
    });

    std::unordered_map<std::string, std::vector<std::string>> students_by_section;
    for (const auto& registration : problem.registrations) {
        students_by_section[registration.section_id].push_back(registration.student_id);
    }

    for (const auto& section : problem.sections) {
        Job job;
        job.section = &section;
        job.instructor = problem.find_instructor(section.instructor_id);
        job.patterns = allowed_patterns(section, config);
        job.weekly_minutes = section.credits * config.minutes_per_credit;
        for (std::size_t r : room_order) {
            const Room& room = problem.rooms[r];
            if (room.capacity >= section.enrolment && (room.is_lab || !section.needs_lab)) job.rooms.push_back(r);
        }
        job.students = students_by_section[section.id];
        std::sort(job.students.begin(), job.students.end());
        job.students.erase(std::unique(job.students.begin(), job.students.end()), job.students.end());
        for (const auto& student : job.students) student_busy_.try_emplace(student, days, slots);
        jobs_.push_back(std::move(job));
    }
}

template <typename Visit>
void Scheduler::for_each_option(const Job& job, Visit visit) const {
    if (job.instructor == nullptr || !within_load(job)) return;
    const int slots = problem_.config.slots_per_day();
    for (const auto& pattern : job.patterns) {
        for (int start = 0; start + pattern.length_slots <= slots; ++start) {
            if (!people_free(job, pattern, start)) continue;
            for (std::size_t room : job.rooms) {
                if (room_busy_[room].is_free(pattern.days, start, pattern.length_slots) &&
                    visit(Option{&pattern, start, room})) {
                    return;
                }
            }
        }
    }
}

std::optional<Option> Scheduler::first_option(const Job& job) const {
    std::optional<Option> found;
    for_each_option(job, [&](const Option& option) {
        found = option;
        return true;
    });
    return found;
}

int Scheduler::count_options(const Job& job) const {
    int count = 0;
    for_each_option(job, [&](const Option&) {
        ++count;
        return false;
    });
    return count;
}

bool Scheduler::within_load(const Job& job) const {
    return instructor_minutes_.at(job.instructor->id) + job.weekly_minutes <= job.instructor->max_weekly_hours * 60;
}

bool Scheduler::people_free(const Job& job, const MeetingPattern& pattern, int start) const {
    if (!instructor_busy_.at(job.instructor->id).is_free(pattern.days, start, pattern.length_slots)) return false;
    for (const auto& student : job.students) {
        if (!student_busy_.at(student).is_free(pattern.days, start, pattern.length_slots)) return false;
    }
    return true;
}

// Explains the first hard rule that blocks a section, from the most basic upward.
std::string Scheduler::why_unplaced(const Job& job) const {
    const Section& section = *job.section;
    if (job.instructor == nullptr) {
        return "instructor '" + section.instructor_id + "' is not in the instructors file";
    }
    if (job.patterns.empty()) {
        return "no allowed meeting pattern fits " + std::to_string(section.credits) + " credit hours";
    }
    if (job.rooms.empty()) {
        int largest = 0;
        for (const auto& room : problem_.rooms) {
            if (room.is_lab || !section.needs_lab) largest = std::max(largest, room.capacity);
        }
        const std::string kind = section.needs_lab ? "lab room" : "room";
        if (largest == 0) return "there is no " + kind;
        return "no " + kind + " holds " + std::to_string(section.enrolment) + " students (largest holds " +
               std::to_string(largest) + ")";
    }
    const std::string& instructor_id = job.instructor->id;
    if (!within_load(job)) {
        const int total = instructor_minutes_.at(instructor_id) + job.weekly_minutes;
        return "instructor " + instructor_id + " would teach " + format_hours(total) + " a week, over the maximum of " +
               std::to_string(job.instructor->max_weekly_hours) + "h";
    }

    bool instructor_free = false;
    bool everyone_free = false;
    const int slots = problem_.config.slots_per_day();
    for (const auto& pattern : job.patterns) {
        for (int start = 0; start + pattern.length_slots <= slots; ++start) {
            if (instructor_busy_.at(instructor_id).is_free(pattern.days, start, pattern.length_slots)) {
                instructor_free = true;
                if (people_free(job, pattern, start)) everyone_free = true;
            }
        }
    }
    if (!instructor_free) return "instructor " + instructor_id + " has no free time for any allowed meeting pattern";
    if (!everyone_free) {
        return "no time when instructor " + instructor_id + " and all " + std::to_string(job.students.size()) +
               " registered students are free";
    }
    return "all suitable rooms (" + std::to_string(job.rooms.size()) + ") are booked whenever the instructor" +
           (job.students.empty() ? " is" : " and students are") + " free";
}

Assignment Scheduler::book(const Job& job, const Option& option) {
    const MeetingPattern& pattern = *option.pattern;
    room_busy_[option.room].book(pattern.days, option.start, pattern.length_slots);
    instructor_busy_.at(job.instructor->id).book(pattern.days, option.start, pattern.length_slots);
    for (const auto& student : job.students) {
        student_busy_.at(student).book(pattern.days, option.start, pattern.length_slots);
    }
    instructor_minutes_.at(job.instructor->id) += job.weekly_minutes;
    return Assignment{job.section->id, problem_.rooms[option.room].id, pattern.days, option.start,
                      pattern.length_slots};
}

ScheduleResult Scheduler::run(Ordering ordering) {
    std::vector<std::size_t> pending(jobs_.size());
    std::iota(pending.begin(), pending.end(), std::size_t{0});
    std::sort(pending.begin(), pending.end(),
              [&](std::size_t a, std::size_t b) { return goes_first(jobs_[a], jobs_[b]); });

    ScheduleResult result;
    while (!pending.empty()) {
        std::size_t pick = 0;
        if (ordering == Ordering::MostConstrained) {
            // Ties keep the static order, so the result stays deterministic.
            int fewest = INT_MAX;
            for (std::size_t k = 0; k < pending.size(); ++k) {
                const int options = count_options(jobs_[pending[k]]);
                if (options < fewest) {
                    fewest = options;
                    pick = k;
                }
            }
        }
        const Job& job = jobs_[pending[pick]];
        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(pick));

        if (const auto option = first_option(job)) {
            result.timetable.assignments.push_back(book(job, *option));
        } else {
            result.unplaced.push_back({job.section->id, why_unplaced(job)});
        }
    }

    std::sort(result.timetable.assignments.begin(), result.timetable.assignments.end(),
              [](const Assignment& a, const Assignment& b) { return a.section_id < b.section_id; });
    std::sort(result.unplaced.begin(), result.unplaced.end(),
              [](const Unplaced& a, const Unplaced& b) { return a.section_id < b.section_id; });
    return result;
}

}  // namespace

ScheduleResult schedule(const Problem& problem, const SchedulerOptions& options) {
    return Scheduler(problem).run(options.ordering);
}

}  // namespace timetable
