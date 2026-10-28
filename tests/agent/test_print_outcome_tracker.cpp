// Which printer report belongs to which send, and the only two facts written
// from them: a print started, and that print finished. GUI-free.

#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Agent/PrintOutcomeTracker.hpp"

#include <string>
#include <vector>

using namespace Slic3r::GUI::JusPrin::Agent;
using State = PrintOutcomeTracker::PrinterState;

namespace {

struct Recorded
{
    std::vector<std::string> started;  // "<hash>:<job>"
    std::vector<std::string> finished; // print ids
};

PrintOutcomeTracker::Writer writer(Recorded& recorded)
{
    return {[&recorded](const PrintOutcomeTracker::AwaitedSend& send, const std::string& job) {
                recorded.started.push_back(send.hash + ":" + job);
                return "p-" + std::to_string(recorded.started.size());
            },
            [&recorded](const PrintOutcomeTracker::FollowedPrint& print) { recorded.finished.push_back(print.print_id); }};
}

PrintOutcomeTracker::AwaitedSend send_to(const std::string& printer, State state = State::Other, const std::string& job = "old")
{
    return {"project-1", "hash-1", "d-1", printer, "Garage X1C", state, job};
}

const auto t0 = PrintOutcomeTracker::Clock::time_point{};

} // namespace

TEST_CASE("a send starts a print when its printer begins a job, and the print finishes", "[print-outcome]")
{
    PrintOutcomeTracker tracker;
    Recorded            recorded;
    tracker.await(send_to("dev-1", State::Finished, "old"), t0);

    // The previous job's finish is still on the printer: not this send's.
    tracker.observe({{"dev-1", State::Finished, "old"}}, writer(recorded), t0);
    // Another printer starting says nothing about this one.
    tracker.observe({{"dev-2", State::Printing, "other"}}, writer(recorded), t0);
    CHECK(recorded.started.empty());

    tracker.observe({{"dev-1", State::Printing, "job-7"}}, writer(recorded), t0);
    CHECK(recorded.started == std::vector<std::string>{"hash-1:job-7"});
    tracker.observe({{"dev-1", State::Printing, "job-7"}}, writer(recorded), t0);
    CHECK(recorded.started.size() == 1);
    CHECK(recorded.finished.empty());

    tracker.observe({{"dev-1", State::Finished, "job-7"}}, writer(recorded), t0);
    CHECK(recorded.finished == std::vector<std::string>{"p-1"});
    // A repeated finish is not a second one.
    tracker.observe({{"dev-1", State::Finished, "job-7"}}, writer(recorded), t0);
    CHECK(recorded.finished.size() == 1);
    CHECK(tracker.idle());
}

TEST_CASE("a print that ends any other way writes nothing", "[print-outcome]")
{
    // FAILED, a person's stop and a return to idle all report as Other.
    const std::string ending = GENERATE("failed", "idle", "another job");
    PrintOutcomeTracker tracker;
    Recorded            recorded;
    tracker.follow({"project-1", "hash-1", "p-1", "dev-1", "job-7"});

    if (ending == "another job")
        tracker.observe({{"dev-1", State::Printing, "job-8"}}, writer(recorded), t0);
    else
        tracker.observe({{"dev-1", State::Other, "job-7"}}, writer(recorded), t0);
    CHECK(tracker.idle());
    // Nor does a later finish of the same job reach a print no longer followed.
    tracker.observe({{"dev-1", State::Finished, "job-7"}}, writer(recorded), t0);
    CHECK(recorded.finished.empty());
    CHECK(recorded.started.empty());
}

TEST_CASE("a job already printing when the send was made is not the send's", "[print-outcome]")
{
    PrintOutcomeTracker tracker;
    Recorded            recorded;
    tracker.await(send_to("dev-1", State::Printing, "busy"), t0);
    tracker.observe({{"dev-1", State::Printing, "busy"}}, writer(recorded), t0);
    CHECK(recorded.started.empty());
    tracker.observe({{"dev-1", State::Printing, "job-9"}}, writer(recorded), t0);
    CHECK(recorded.started == std::vector<std::string>{"hash-1:job-9"});
}

TEST_CASE("a send no printer starts is no longer awaited", "[print-outcome]")
{
    PrintOutcomeTracker tracker;
    Recorded            recorded;
    tracker.await(send_to("dev-1"), t0);
    tracker.observe({}, writer(recorded), t0 + PrintOutcomeTracker::kStartWindow + std::chrono::seconds(1));
    CHECK(tracker.idle());
    tracker.observe({{"dev-1", State::Printing, "late"}}, writer(recorded), t0 + PrintOutcomeTracker::kStartWindow * 2);
    CHECK(recorded.started.empty());
}

TEST_CASE("the newer of two sends to one printer is the one its next job runs", "[print-outcome]")
{
    PrintOutcomeTracker tracker;
    Recorded            recorded;
    tracker.await(send_to("dev-1"), t0);
    PrintOutcomeTracker::AwaitedSend newer = send_to("dev-1");
    newer.hash = "hash-2";
    tracker.await(newer, t0);
    tracker.observe({{"dev-1", State::Printing, "job-1"}}, writer(recorded), t0);
    CHECK(recorded.started == std::vector<std::string>{"hash-2:job-1"});
}

TEST_CASE("following a print twice follows it once", "[print-outcome]")
{
    PrintOutcomeTracker tracker;
    Recorded            recorded;
    tracker.follow({"project-1", "hash-1", "p-1", "dev-1", "job-7"});
    tracker.follow({"project-1", "hash-1", "p-1", "dev-1", "job-7"});
    tracker.observe({{"dev-1", State::Finished, "job-7"}}, writer(recorded), t0);
    CHECK(recorded.finished == std::vector<std::string>{"p-1"});
}
