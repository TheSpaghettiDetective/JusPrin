#include "McpConfigFile.hpp"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fstream>
#include <mutex>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Slic3r::GUI::JusPrin::Mcp {
namespace fs = std::filesystem;
using nlohmann::json;
namespace {
std::mutex config_mutex;

std::optional<std::string> read_config(const fs::path& path)
{
    if (fs::is_symlink(fs::symlink_status(path)))
        throw std::runtime_error("Client configuration is a symbolic link. Use Copy and update its target manually.");
    if (!fs::exists(path)) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read client configuration: " + path.u8string());
    std::string bytes(1024 * 1024 + 1, '\0');
    file.read(bytes.data(), bytes.size());
    if (file.bad()) throw std::runtime_error("Failed reading client configuration: " + path.u8string());
    bytes.resize(std::size_t(file.gcount()));
    if (bytes.size() > 1024 * 1024) throw std::runtime_error("Client configuration exceeds the 1 MiB setup limit.");
    return bytes;
}

// Mask comments and trailing commas without changing offsets. Validate with
// the JSON library before locating spans; this is not a second JSON parser.
std::string jsonc(const std::string& source)
{
    std::string text = source;
    // Some editors save UTF-8 with a BOM. Keep its bytes in the original,
    // but treat it as whitespace when finding JSON member offsets.
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.replace(0, 3, "   ");
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 32) throw std::runtime_error("Client configuration exceeds nesting depth 32.");
        } else if (c == '}' || c == ']') { if (depth) --depth; }
        else if (c == '/' && i + 1 < text.size() && (text[i + 1] == '/' || text[i + 1] == '*')) {
            const bool block = text[i + 1] == '*';
            text[i++] = ' '; text[i] = ' ';
            bool closed = !block;
            while (++i < text.size()) {
                if (!block && (text[i] == '\n' || text[i] == '\r')) break;
                if (block && text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
                    text[i++] = ' '; text[i] = ' '; closed = true; break;
                }
                if (text[i] != '\n' && text[i] != '\r') text[i] = ' ';
            }
            if (!closed) throw std::runtime_error("Client configuration has an unterminated comment.");
        }
    }
    quoted = false; escaped = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == ',') {
            const auto next = text.find_first_not_of(" \t\r\n", i + 1);
            if (next != std::string::npos && (text[next] == '}' || text[next] == ']')) text[i] = ' ';
        }
    }
    return text;
}

struct Span { std::size_t begin, end; };
std::size_t value_end(const std::string& text, std::size_t start)
{
    bool quoted = false, escaped = false;
    unsigned depth = 0;
    for (auto i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') { quoted = false; if (depth == 0) return i + 1; }
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') { if (depth == 0) return i; if (--depth == 0) return i + 1; }
        else if (depth == 0 && (c == ',' || std::isspace(static_cast<unsigned char>(c)))) return i;
    }
    return text.size();
}

std::optional<Span> member(const std::string& text, Span object, const std::string& wanted)
{
    auto i = object.begin + 1;
    while ((i = text.find_first_not_of(" \t\r\n,", i)) < object.end - 1) {
        const auto key_end = value_end(text, i);
        const auto key = json::parse(text.substr(i, key_end - i)).get<std::string>();
        const auto colon = text.find(':', key_end);
        const auto start = text.find_first_not_of(" \t\r\n", colon + 1);
        const auto end = value_end(text, start);
        if (key == wanted) return Span{start, end};
        i = end;
    }
    return std::nullopt;
}

void insert_member(std::string& source, const std::string& masked, Span object, const std::string& key, const json& value)
{
    // Insert immediately after '{'. Existing comments/trailing commas remain
    // byte-for-byte intact, with a separator only when the object has members.
    const auto parsed = json::parse(masked.substr(object.begin, object.end - object.begin));
    source.insert(object.begin + 1, "\n" + json(key).dump() + ": " + value.dump(2) + (parsed.empty() ? "\n" : ",\n"));
}

void write_file(const fs::path& path, const std::string& bytes, fs::perms permissions)
{
    std::ofstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.open(path, std::ios::binary | std::ios::trunc);
#ifndef _WIN32
    fs::permissions(path, permissions);
#endif
    out << bytes;
    out.close();
}
}

ConfigEdit prepare_json_connection(const fs::path& path, const std::string& root, const json& server)
{
    if (!server.is_object() || (root != "mcpServers" && root != "servers"))
        throw std::invalid_argument("Invalid MCP client configuration entry");
    ConfigEdit edit{path, read_config(path), {}, {}};
    const std::string source = edit.before.value_or("{}\n");
    const auto masked = jsonc(source);
    const auto original = json::parse(masked);
    if (!original.is_object() || (original.contains(root) && !original[root].is_object()))
        throw std::runtime_error("Client configuration and its server map must be JSON objects. No changes made.");
    auto expected = original;
    expected[root]["jusprin"] = server;
    edit.after = source;
    const auto start = masked.find_first_not_of(" \t\r\n");
    const Span document{start, value_end(masked, start)};
    if (const auto servers = member(masked, document, root)) {
        if (const auto current = member(masked, *servers, "jusprin"))
            edit.after.replace(current->begin, current->end - current->begin, server.dump(2));
        else insert_member(edit.after, masked, *servers, "jusprin", server);
    } else insert_member(edit.after, masked, document, root, json{{"jusprin", server}});
    if (json::parse(jsonc(edit.after)) != expected)
        throw std::runtime_error("Client configuration has ambiguous duplicate keys. No changes made.");
    const auto previous = original.contains(root) && original[root].contains("jusprin") ? original[root]["jusprin"] : json();
    if (previous == server) edit.after = source;
    edit.preview = "File: " + path.u8string() + "\n\nPrevious JusPrin entry:\n" + previous.dump(2) +
        "\n\nNew JusPrin entry:\n" + server.dump(2) +
        "\n\nOnly the JusPrin entry changes. Other settings and comments are preserved. An existing file is backed up.";
    return edit;
}

fs::path apply_config_edit(const ConfigEdit& edit)
{
    std::lock_guard<std::mutex> guard(config_mutex);
    if (read_config(edit.path) != edit.before)
        throw std::runtime_error("Client configuration changed after the preview. Review the new change and confirm again.");
    if (edit.before && *edit.before == edit.after) return {};
    fs::create_directories(edit.path.parent_path());
    const auto suffix = boost::uuids::to_string(boost::uuids::random_generator()());
    const auto temporary = fs::u8path(edit.path.u8string() + ".jusprin-" + suffix + ".tmp");
    fs::path backup;
    const auto permissions = edit.before ? fs::status(edit.path).permissions() : fs::perms::owner_read | fs::perms::owner_write;
    if (edit.before) {
        backup = fs::u8path(edit.path.u8string() + ".jusprin-backup-" + suffix);
        write_file(backup, *edit.before, fs::perms::owner_read | fs::perms::owner_write);
    }
    write_file(temporary, edit.after, permissions);
    if (read_config(edit.path) != edit.before)
        throw std::runtime_error("Client configuration changed while preparing the write. Original file retained; review again.");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), edit.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw fs::filesystem_error("Replace client configuration", temporary, edit.path,
                                   std::error_code(GetLastError(), std::system_category()));
#else
    fs::rename(temporary, edit.path);
#endif
    return backup;
}
} // namespace Slic3r::GUI::JusPrin::Mcp
