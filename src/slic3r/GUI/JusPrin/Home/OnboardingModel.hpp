#pragma once

// Pure state-gating for first-run onboarding. The Orca adapter supplies facts
// from the loaded preset bundle and project; this model only decides which
// missing outcome should be presented next.

namespace Slic3r::GUI::JusPrin::Home {

enum class OnboardingStatus { Unfinished, Completed, Dismissed };
enum class OnboardingStep { Hidden, Welcome, Profiles, ConfirmSetup, Setup, Project };

struct OnboardingProgress
{
    OnboardingStatus status{OnboardingStatus::Unfinished};
    bool started{false};
    bool profile_import_deferred{false};
    bool partial_import_pending{false};
    bool setup_confirmed{false};
    bool offline_example{false};
};

struct OnboardingFacts
{
    bool profiles_available{false};
    bool usable_setup{false};
    bool project_open{false};
};

OnboardingStep next_onboarding_step(const OnboardingProgress& progress, const OnboardingFacts& facts);
const char* to_string(OnboardingStatus status);
const char* to_string(OnboardingStep step);

} // namespace Slic3r::GUI::JusPrin::Home
