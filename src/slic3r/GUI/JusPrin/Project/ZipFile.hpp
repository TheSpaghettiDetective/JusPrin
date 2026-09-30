#pragma once

// Thin RAII wrappers over miniz for the project archives JusPrin reads and
// writes on its own: checkpoints, their mesh files, and the verification of
// a saved project. Entry names are UTF-8; an entry OrcaSlicer wrote on
// Windows carries its UTF-8 name in the 0x7075 extra field, which is read
// here the way OrcaSlicer's own loader reads it.
//
// GUI-free. Every function reports failure through its return value; none
// throws for a missing or damaged file.

#include <miniz.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Project {

class ZipReader
{
public:
    ZipReader() = default;
    ~ZipReader();
    ZipReader(const ZipReader&)            = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    bool open(const std::filesystem::path& path);
    bool is_open() const { return m_file != nullptr; }

    std::size_t entry_count() const;
    // The entry's UTF-8 name: the unicode extra field when present, else the
    // name in the central directory.
    std::string entry_name(std::size_t index) const;
    std::optional<std::size_t> find(const std::string& name) const;
    // Decompressed size and CRC as the central directory records them.
    bool stat(std::size_t index, mz_zip_archive_file_stat& stat) const;
    std::optional<std::string> read(std::size_t index) const;
    std::optional<std::string> read(const std::string& name) const;
    // Inflates the entry and compares its CRC: what a save must survive.
    bool validate(std::size_t index) const;

    mz_zip_archive& archive() { return m_archive; }

private:
    mz_zip_archive m_archive{};
    FILE*          m_file{nullptr};
};

class ZipWriter
{
public:
    ZipWriter() = default;
    ~ZipWriter();
    ZipWriter(const ZipWriter&)            = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    // Writes to `path`.tmp; finish() renames it into place. An unfinished
    // writer removes its temporary file.
    bool open(const std::filesystem::path& path);
    bool add(const std::string& name, const std::string& bytes, bool compress = true);
    // Copies one entry, compressed bytes and all, without inflating it.
    bool add_from(ZipReader& source, std::size_t index);
    // Finalizes, closes, checks the close, and renames over `path`. False
    // leaves nothing at `path` that was not there before.
    bool finish();

private:
    void discard();

    mz_zip_archive        m_archive{};
    FILE*                 m_file{nullptr};
    std::filesystem::path m_path;
    std::filesystem::path m_temp;
    bool                  m_open{false};
};

} // namespace Slic3r::GUI::JusPrin::Project
