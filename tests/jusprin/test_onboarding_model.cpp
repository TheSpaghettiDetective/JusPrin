#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Home/OnboardingModel.hpp"

using namespace Slic3r::GUI::JusPrin::Home;

TEST_CASE("onboarding gates are derived in local outcome order", "[home][onboarding]")
{
    OnboardingProgress progress;
    OnboardingFacts facts;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Welcome);

    progress.started = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Profiles);
    progress.profile_import_deferred = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Setup);
    progress.offline_example = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Project);
    facts.project_open = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Hidden);
}

TEST_CASE("authoritative outcomes skip resolved onboarding gates", "[home][onboarding]")
{
    OnboardingProgress progress;
    progress.started = true;
    OnboardingFacts facts{true, true, true};
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::ConfirmSetup);
    progress.setup_confirmed = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Hidden);

    facts.project_open = false;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Project);
    facts.usable_setup = false;
    progress.setup_confirmed = false;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Setup);
    facts.profiles_available = false;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Profiles);
}

TEST_CASE("completed and dismissed onboarding never opens automatically", "[home][onboarding]")
{
    OnboardingFacts facts;
    OnboardingProgress progress;
    progress.status = OnboardingStatus::Completed;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Hidden);

    progress.status = OnboardingStatus::Dismissed;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Hidden);
}

TEST_CASE("a partial import stays on profiles until it is acknowledged", "[home][onboarding]")
{
    OnboardingProgress progress;
    progress.started                = true;
    progress.partial_import_pending = true;
    OnboardingFacts facts{true, true, false};
    progress.setup_confirmed = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Profiles);

    progress.partial_import_pending = false;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Project);
}
