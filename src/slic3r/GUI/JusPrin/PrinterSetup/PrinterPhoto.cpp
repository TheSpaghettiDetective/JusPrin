#include "PrinterPhoto.hpp"

namespace Slic3r::GUI::JusPrin::PrinterSetup {

PhotoCheck check_photo(const std::string& bytes)
{
    if (bytes.empty()) return {{}, "That photo is empty. Choose another file."};
    if (bytes.size() > kMaximumPhotoBytes)
        return {{}, "That photo is larger than 10 MB. Choose a smaller image."};
    auto starts_with = [&](std::size_t offset, const std::string& signature) {
        return bytes.size() >= offset + signature.size() && bytes.compare(offset, signature.size(), signature) == 0;
    };
    if (starts_with(0, std::string("\x89PNG\r\n\x1a\n", 8))) return {"image/png", {}};
    if (starts_with(0, "\xFF\xD8\xFF")) return {"image/jpeg", {}};
    if (starts_with(0, "RIFF") && starts_with(8, "WEBP")) return {"image/webp", {}};
    return {{}, "Choose a PNG, JPEG, or WebP image."};
}

} // namespace Slic3r::GUI::JusPrin::PrinterSetup
