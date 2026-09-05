#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

// Runtime identity of an actual Orca G-code result, not the workspace revision
// (selection alone changes that revision). Never serialize this across runs.
struct SliceIdentity
{
    std::uint64_t session{0}, plate{0}, result{0};
    bool valid() const { return session && plate && result; }
    bool operator==(const SliceIdentity& other) const
    { return std::tie(session, plate, result) == std::tie(other.session, other.plate, other.result); }
};

class SliceReviews
{
public:
    // Reporting is presentation metadata, not a model/settings mutation or a
    // safety certification. Identical retries must not undo the user's review.
    bool report(SliceIdentity expected, SliceIdentity current, std::vector<std::string> findings)
    {
        if (!expected.valid() || !(expected == current)) return false;
        if (m_session != current.session) {
            m_reviews.clear();
            m_session = current.session;
        }
        auto& review = m_reviews[current.plate];
        if (!(review.identity == current) || review.findings != findings)
            review = {current, std::move(findings), false};
        if (m_listener) m_listener();
        return true;
    }

    bool needs_review(SliceIdentity current) const
    {
        const auto* review = find(current);
        return review && !review->reviewed && !review->findings.empty();
    }

    void acknowledge(SliceIdentity current)
    {
        if (find(current)) {
            m_reviews.at(current.plate).reviewed = true;
            if (m_listener) m_listener();
        }
    }

    void set_listener(std::function<void()> listener) { m_listener = std::move(listener); }

private:
    struct Review { SliceIdentity identity; std::vector<std::string> findings; bool reviewed{false}; };
    const Review* find(SliceIdentity current) const
    {
        const auto found = m_reviews.find(current.plate);
        return current.valid() && found != m_reviews.end() && found->second.identity == current ? &found->second : nullptr;
    }
    std::uint64_t m_session{0};
    std::map<std::uint64_t, Review> m_reviews;
    std::function<void()> m_listener;
};

} // namespace Slic3r::GUI::JusPrin::Workspace
