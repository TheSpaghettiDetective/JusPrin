#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Project/AutosavePolicy.hpp"

using namespace Slic3r::GUI::JusPrin::Project;

TEST_CASE("A fresh policy writes nothing", "[AutosavePolicy]")
{
    AutosavePolicy policy;
    const auto     decision = policy.decide(100.0);
    REQUIRE_FALSE(decision.full_save);
    REQUIRE_FALSE(decision.checkpoint);
    REQUIRE_FALSE(policy.dirty_since_full_save());
}

TEST_CASE("An opened project owes an opening checkpoint and no save", "[AutosavePolicy]")
{
    AutosavePolicy policy;
    policy.project_opened(10.0);
    auto decision = policy.decide(10.0);
    REQUIRE(decision.checkpoint);
    REQUIRE(decision.checkpoint_reason == "opened");
    REQUIRE_FALSE(decision.full_save);

    policy.checkpointed(10.1);
    decision = policy.decide(11.0);
    REQUIRE_FALSE(decision.checkpoint);
    REQUIRE_FALSE(policy.dirty_since_checkpoint());
}

TEST_CASE("Edits alone never start a full save", "[AutosavePolicy]")
{
    AutosavePolicy policy;
    policy.project_opened(0.0);
    policy.checkpointed(0.0);
    policy.edited(5.0);
    REQUIRE(policy.dirty_since_full_save());
    REQUIRE_FALSE(policy.decide(5.0).full_save);
    REQUIRE_FALSE(policy.decide(5000.0).full_save);
}

TEST_CASE("Focus loss and closing save only when something changed", "[AutosavePolicy]")
{
    AutosavePolicy policy;
    policy.project_opened(0.0);
    policy.checkpointed(0.0);
    policy.focus_lost(1.0);
    REQUIRE_FALSE(policy.decide(1.0).full_save);

    policy.edited(2.0);
    policy.focus_lost(3.0);
    REQUIRE(policy.decide(3.0).full_save);
    // A failed write leaves the decision standing.
    REQUIRE(policy.decide(4.0).full_save);
    policy.full_saved(4.0);
    REQUIRE_FALSE(policy.decide(5.0).full_save);
    REQUIRE_FALSE(policy.dirty_since_full_save());

    policy.edited(6.0);
    policy.close_requested(7.0);
    REQUIRE(policy.decide(7.0).full_save);
}

TEST_CASE("An idle full save is off unless configured", "[AutosavePolicy]")
{
    AutosavePolicy::Settings settings;
    settings.idle_full_save_seconds = 300.0;
    AutosavePolicy policy(settings);
    policy.project_opened(0.0);
    policy.checkpointed(0.0);
    policy.edited(10.0);
    REQUIRE_FALSE(policy.decide(309.0).full_save);
    REQUIRE(policy.decide(310.0).full_save);
    policy.full_saved(310.0);
    REQUIRE_FALSE(policy.decide(10000.0).full_save);
}

TEST_CASE("A checkpoint precedes an agent action only when edits happened since the last one", "[AutosavePolicy]")
{
    AutosavePolicy policy;
    policy.project_opened(0.0);
    policy.checkpointed(0.0);
    policy.agent_action_starting(1.0);
    REQUIRE_FALSE(policy.decide(1.0).checkpoint);

    policy.edited(2.0);
    policy.agent_action_starting(2.5);
    auto decision = policy.decide(2.5);
    REQUIRE(decision.checkpoint);
    REQUIRE(decision.checkpoint_reason == "agent");
    policy.checkpointed(2.6);
    REQUIRE_FALSE(policy.decide(3.0).checkpoint);
}

TEST_CASE("The opening checkpoint is not renamed by an agent action", "[AutosavePolicy]")
{
    AutosavePolicy policy;
    policy.project_opened(0.0);
    policy.agent_action_starting(0.5);
    REQUIRE(policy.decide(0.5).checkpoint_reason == "opened");
}

TEST_CASE("Manual editing checkpoints wait for edits to settle and for the interval", "[AutosavePolicy]")
{
    AutosavePolicy::Settings settings;
    settings.settle_seconds              = 3.0;
    settings.editing_checkpoint_interval = 600.0;
    AutosavePolicy policy(settings);
    policy.project_opened(0.0);
    policy.checkpointed(0.0);

    policy.edited(100.0);
    REQUIRE_FALSE(policy.decide(102.0).checkpoint); // still settling
    REQUIRE_FALSE(policy.decide(103.0).checkpoint); // settled, interval not reached
    REQUIRE_FALSE(policy.decide(599.0).checkpoint);
    auto decision = policy.decide(600.0);
    REQUIRE(decision.checkpoint);
    REQUIRE(decision.checkpoint_reason == "editing");

    policy.edited(600.5); // a burst in progress waits
    REQUIRE_FALSE(policy.decide(601.0).checkpoint);
    REQUIRE(policy.decide(603.5).checkpoint);
    policy.checkpointed(603.5);
    REQUIRE_FALSE(policy.decide(700.0).checkpoint);

    policy.edited(700.0);
    REQUIRE_FALSE(policy.decide(1000.0).checkpoint); // within the interval of the last one
    REQUIRE(policy.decide(1203.5).checkpoint);
}

TEST_CASE("Opening another project drops what the old one owed", "[AutosavePolicy]")
{
    AutosavePolicy policy;
    policy.project_opened(0.0);
    policy.checkpointed(0.0);
    policy.edited(1.0);
    policy.focus_lost(2.0);
    REQUIRE(policy.decide(2.0).full_save);
    policy.project_opened(3.0);
    const auto decision = policy.decide(3.0);
    REQUIRE_FALSE(decision.full_save);
    REQUIRE(decision.checkpoint_reason == "opened");
}
