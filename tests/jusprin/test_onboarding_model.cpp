#include <catch2/catch_all.hpp>

#include "slic3r/GUI/JusPrin/Home/OnboardingModel.hpp"

using namespace Slic3r::GUI::JusPrin::Home;

TEST_CASE("a first launch runs welcome, then printers, then home", "[home][onboarding]")
{
    OnboardingProgress progress;
    OnboardingFacts facts;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Welcome);

    // Any door, "Just slice for now" included, leaves the welcome.
    progress.welcome_left = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Printers);

    progress.printer_chosen = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Hidden);
}

TEST_CASE("back from printers returns to the welcome", "[home][onboarding]")
{
    OnboardingProgress progress;
    OnboardingFacts facts;
    progress.welcome_left = true;
    REQUIRE(next_onboarding_step(progress, facts) == OnboardingStep::Printers);
    progress.welcome_left = false;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Welcome);
}

TEST_CASE("the welcome shows once even with something already set up", "[home][onboarding]")
{
    OnboardingProgress progress;
    OnboardingFacts facts;
    facts.v1_printers       = true;
    facts.printer_from_file = true;
    facts.orca_printers     = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Welcome);
}

TEST_CASE("a file's printer skips the printers step, and JusPrin 1 on this computer does not", "[home][onboarding]")
{
    OnboardingProgress progress;
    progress.welcome_left = true;

    // JusPrin 1's printers are brought only when the person asks.
    OnboardingFacts v1;
    v1.v1_printers = true;
    CHECK(next_onboarding_step(progress, v1) == OnboardingStep::Printers);

    OnboardingFacts file;
    file.printer_from_file = true;
    CHECK(next_onboarding_step(progress, file) == OnboardingStep::Hidden);
}

TEST_CASE("completed onboarding never shows again", "[home][onboarding]")
{
    OnboardingProgress progress;
    progress.status = OnboardingStatus::Completed;
    CHECK(next_onboarding_step(progress, OnboardingFacts{}) == OnboardingStep::Hidden);
}

TEST_CASE("the printers step offers JusPrin 1's printers first, then OrcaSlicer's, then the printer screen", "[home][onboarding]")
{
    OnboardingProgress progress;
    OnboardingFacts facts;
    CHECK(printers_screen(progress, facts) == PrintersScreen::AddPrinter);

    facts.orca_printers = true;
    CHECK(printers_screen(progress, facts) == PrintersScreen::OrcaImport);

    facts.v1_printers = true;
    CHECK(printers_screen(progress, facts) == PrintersScreen::V1Import);
}

TEST_CASE("an other-ways link opens the way it names while that way has something to show", "[home][onboarding]")
{
    OnboardingProgress progress;
    OnboardingFacts facts;
    facts.v1_printers   = true;
    facts.orca_printers = true;

    progress.way = PrintersWay::OrcaImport;
    CHECK(printers_screen(progress, facts) == PrintersScreen::OrcaImport);
    progress.way = PrintersWay::AddPrinter;
    CHECK(printers_screen(progress, facts) == PrintersScreen::AddPrinter);
    progress.way = PrintersWay::V1Import;
    CHECK(printers_screen(progress, facts) == PrintersScreen::V1Import);

    // A way whose folder is gone, as after a relaunch forgot a folder picked
    // by hand, shows what the app found instead.
    facts.v1_printers = false;
    CHECK(printers_screen(progress, facts) == PrintersScreen::OrcaImport);

    for (PrintersWay way : {PrintersWay::None, PrintersWay::V1Import, PrintersWay::OrcaImport, PrintersWay::AddPrinter})
        CHECK(printers_way_from_string(to_string(way)) == way);
    CHECK(printers_way_from_string("fresh_start") == PrintersWay::None);
}

TEST_CASE("an import's result holds the printers step until continue", "[home][onboarding]")
{
    OnboardingProgress progress;
    progress.welcome_left = true;
    OnboardingFacts facts;
    facts.orca_printers      = true;
    facts.import_result_open = true;
    // The import brought a printer, which alone would end the sequence.
    progress.printer_chosen = true;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Printers);
    CHECK(printers_screen(progress, facts) == PrintersScreen::ImportResult);

    facts.import_result_open = false;
    CHECK(next_onboarding_step(progress, facts) == OnboardingStep::Hidden);
}

TEST_CASE("home's agent group follows the agent's setup", "[home][onboarding]")
{
    CHECK(home_agent_card({false, false, false, false}) == HomeAgentCard::Doors);
    // Signed in: the account and its credits, once.
    CHECK(home_agent_card({true, true, false, false}) == HomeAgentCard::Account);
    CHECK(home_agent_card({true, true, true, false}) == HomeAgentCard::None);
    // Your own key or an AI tool: nothing to show.
    CHECK(home_agent_card({true, false, false, false}) == HomeAgentCard::None);
}

TEST_CASE("set up AI stays off home once the person asks never to see it again", "[home][onboarding]")
{
    CHECK(home_agent_card({false, false, false, true}) == HomeAgentCard::None);
    // Setting AI up later from Preferences still shows the account once.
    CHECK(home_agent_card({true, true, false, true}) == HomeAgentCard::Account);
}
