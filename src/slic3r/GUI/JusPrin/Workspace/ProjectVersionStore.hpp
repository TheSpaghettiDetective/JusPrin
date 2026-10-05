#pragma once

#include "libslic3r/Model.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

// The store owns immutable versions of an Orca project. Its caller captures
// on the GUI thread and runs publish() on one worker at a time. A new capture
// is allowed only after that worker has completed and its result is observed.
class ProjectVersionStore
{
public:
    enum class PublishStage { ResourcesDurable, VersionPublished, HeadPublished };
    struct Resource
    {
        std::string id;
        std::string entry;
    };

    struct ObjectResource
    {
        std::uint64_t object_id{0};
        int backup_id{0};
        std::size_t facets{0};
        std::string entry;
        Resource resource;
    };

    struct Version
    {
        std::string id;
        std::uint64_t revision{0};
        // Last raw change included in this immutable model snapshot. Zero
        // means an older manifest without a known timeline boundary.
        std::uint64_t change_seq{0};
        std::string created_at;
        std::string source_path;
        std::vector<ObjectResource> objects;
        std::string project_name;
    };

    struct Capture
    {
        Version version;
        std::filesystem::path pending_dir;
        std::shared_ptr<Model> frozen;
        std::vector<std::uint64_t> changed_objects;
        std::map<std::uint64_t, std::string> signatures;
    };

    struct Materialization
    {
        std::filesystem::path directory;
        std::filesystem::path metadata;
        std::filesystem::path absent_original;
        std::string semantic_state;
        bool empty{false};
    };

    ProjectVersionStore(std::filesystem::path root, std::string project_id);
    ~ProjectVersionStore();
    ProjectVersionStore(const ProjectVersionStore&) = delete;
    ProjectVersionStore& operator=(const ProjectVersionStore&) = delete;

    const std::string& project_id() const { return m_project_id; }
    const std::string& head_id() const { return m_head_id; }
    std::filesystem::path pending_metadata_path(const std::string& version_id) const;

    // export_3mf(Backup | Silence) must have completed at metadata_path before
    // freeze() is called. freeze() preserves Orca backup IDs in its copy.
    Capture freeze(Model& live, const std::string& version_id, std::uint64_t revision,
                   std::string semantic_state, std::string source_path, std::string created_at);
    bool unchanged(const Capture& capture) const;
    void discard(const Capture& capture) const;
    void publish(Capture capture);
    // The current project document advances independently of model history.
    // Publishing it before a model version keeps both durable across a crash.
    void publish_document(std::string semantic_state);
    void ensure_durable_head();
    void set_publish_stage_hook(std::function<void(PublishStage)> hook) { m_publish_stage_hook = std::move(hook); }
    void seed_from_live(Model& live);
    Materialization materialize(const std::string& version_id) const;
    void remove_materialization(const Materialization& materialization) const;
    std::vector<Version> history() const;
    void pin(const std::string& version_id);
    // A chat restore spans Orca model publication and the independent
    // document write. An interrupted marker blocks reopening rather than
    // exposing a chat against a possibly mismatched model.
    void begin_chat_restore(const std::string& from_version, const std::string& to_version,
                            const std::string& conversation_id);
    void finish_chat_restore();
    void record_operation(const std::string& action_id, const std::string& tool, const std::string& outcome,
                          const std::string& before, const std::string& after);
    void prune(std::size_t max_versions, std::uintmax_t max_bytes,
               const std::set<std::string>& live_attachments = {});

    static std::string new_id();
    static void validate_export_archive(const std::filesystem::path& archive);
    static void publish_export_archive(const std::filesystem::path& temporary,
                                       const std::filesystem::path& destination);

private:
    std::filesystem::path version_dir(const std::string& id) const;
    std::filesystem::path resource_path(const std::string& id) const;
    Version read_version(const std::string& id) const;
    void load_head();
    void recover_uncommitted();
    void lock();
    void unlock();

    std::filesystem::path m_root;
    std::string m_project_id;
    std::string m_head_id;
    std::vector<std::string> m_committed_ids;
    std::map<std::uint64_t, ObjectResource> m_last_resources;
    std::map<std::uint64_t, std::string> m_last_signatures;
    std::shared_ptr<Model> m_last_model;
    std::string m_last_metadata_digest;
    std::string m_last_semantic_state;
    int m_document_schema{0};
    bool m_head_needs_sync{false};
    std::function<void(PublishStage)> m_publish_stage_hook;
#ifdef _WIN32
    void* m_lock_handle{nullptr};
#else
    int m_lock_fd{-1};
#endif
};

} // namespace Slic3r::GUI::JusPrin::Workspace
