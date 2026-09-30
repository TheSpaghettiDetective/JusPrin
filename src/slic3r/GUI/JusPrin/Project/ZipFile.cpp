#include "ZipFile.hpp"

#include <cstdio>
#include <cstring>
#include <system_error>

namespace Slic3r::GUI::JusPrin::Project {

namespace {

FILE* open_file(const std::filesystem::path& path, const char* mode)
{
#ifdef _WIN32
    std::wstring wide_mode(mode, mode + std::strlen(mode));
    return _wfopen(path.c_str(), wide_mode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

// The 0x7075 "Info-ZIP Unicode Path" field OrcaSlicer writes when an entry's
// native name differs from its UTF-8 name.
std::optional<std::string> unicode_name(const std::string& extra)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(extra.data());
    const unsigned char* e = p + extra.size();
    while (p + 4 <= e) {
        const std::uint16_t len = std::uint16_t(p[2]) | std::uint16_t(p[3] << 8);
        if (p[0] == 0x75 && p[1] == 0x70 && len >= 5 && p + 4 + len <= e && p[4] == 0x01)
            return std::string(reinterpret_cast<const char*>(p + 9), reinterpret_cast<const char*>(p + 4 + len));
        p += 4 + len;
    }
    return std::nullopt;
}

} // namespace

// -- ZipReader ---------------------------------------------------------------

ZipReader::~ZipReader()
{
    if (m_file != nullptr) {
        mz_zip_reader_end(&m_archive);
        std::fclose(m_file);
    }
}

bool ZipReader::open(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto      size = std::filesystem::file_size(path, ec);
    if (ec)
        return false;
    m_file = open_file(path, "rb");
    if (m_file == nullptr)
        return false;
    mz_zip_zero_struct(&m_archive);
    if (!mz_zip_reader_init_cfile(&m_archive, m_file, size, 0)) {
        std::fclose(m_file);
        m_file = nullptr;
        return false;
    }
    return true;
}

std::size_t ZipReader::entry_count() const
{
    return m_file == nullptr ? 0 : mz_zip_reader_get_num_files(const_cast<mz_zip_archive*>(&m_archive));
}

std::string ZipReader::entry_name(std::size_t index) const
{
    mz_zip_archive* archive = const_cast<mz_zip_archive*>(&m_archive);
    std::string     extra(1024, '\0');
    const mz_uint   n = mz_zip_reader_get_extra(archive, mz_uint(index), extra.data(), mz_uint(extra.size()));
    if (n > 0)
        if (const auto name = unicode_name(extra.substr(0, n)))
            return *name;
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(archive, mz_uint(index), &stat))
        return {};
    return stat.m_filename;
}

std::optional<std::size_t> ZipReader::find(const std::string& name) const
{
    std::string wanted = name;
    if (!wanted.empty() && wanted.front() == '/')
        wanted.erase(0, 1);
    for (std::size_t i = 0; i < entry_count(); ++i)
        if (entry_name(i) == wanted)
            return i;
    return std::nullopt;
}

bool ZipReader::stat(std::size_t index, mz_zip_archive_file_stat& stat) const
{
    return mz_zip_reader_file_stat(const_cast<mz_zip_archive*>(&m_archive), mz_uint(index), &stat);
}

std::optional<std::string> ZipReader::read(std::size_t index) const
{
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(const_cast<mz_zip_archive*>(&m_archive), mz_uint(index), &size, 0);
    if (data == nullptr)
        return std::nullopt;
    std::string bytes(static_cast<const char*>(data), size);
    mz_free(data);
    return bytes;
}

std::optional<std::string> ZipReader::read(const std::string& name) const
{
    const auto index = find(name);
    return index ? read(*index) : std::nullopt;
}

bool ZipReader::validate(std::size_t index) const
{
    return mz_zip_validate_file(const_cast<mz_zip_archive*>(&m_archive), mz_uint(index), 0);
}

// -- ZipWriter ---------------------------------------------------------------

ZipWriter::~ZipWriter()
{
    discard();
}

bool ZipWriter::open(const std::filesystem::path& path)
{
    m_path = path;
    m_temp = path;
    m_temp += ".tmp";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::remove(m_temp, ec);
    m_file = open_file(m_temp, "wb");
    if (m_file == nullptr)
        return false;
    mz_zip_zero_struct(&m_archive);
    if (!mz_zip_writer_init_cfile(&m_archive, m_file, 0)) {
        std::fclose(m_file);
        m_file = nullptr;
        std::filesystem::remove(m_temp, ec);
        return false;
    }
    m_open = true;
    return true;
}

bool ZipWriter::add(const std::string& name, const std::string& bytes, bool compress)
{
    return m_open && mz_zip_writer_add_mem(&m_archive, name.c_str(), bytes.data(), bytes.size(),
                                           compress ? MZ_DEFAULT_LEVEL : MZ_NO_COMPRESSION);
}

bool ZipWriter::add_from(ZipReader& source, std::size_t index)
{
    return m_open && mz_zip_writer_add_from_zip_reader(&m_archive, &source.archive(), mz_uint(index));
}

bool ZipWriter::finish()
{
    if (!m_open)
        return false;
    // Each step reports its own failure; a full disk shows up in any of them,
    // and OrcaSlicer's exporter misses the last two.
    bool ok = mz_zip_writer_finalize_archive(&m_archive);
    ok      = mz_zip_writer_end(&m_archive) && ok;
    ok      = std::fclose(m_file) == 0 && ok;
    m_file  = nullptr;
    m_open  = false;
    std::error_code ec;
    if (ok) {
        std::filesystem::rename(m_temp, m_path, ec);
        ok = !ec;
    }
    if (!ok)
        std::filesystem::remove(m_temp, ec);
    return ok;
}

void ZipWriter::discard()
{
    if (!m_open)
        return;
    mz_zip_writer_end(&m_archive);
    std::fclose(m_file);
    m_file = nullptr;
    m_open = false;
    std::error_code ec;
    std::filesystem::remove(m_temp, ec);
}

} // namespace Slic3r::GUI::JusPrin::Project
