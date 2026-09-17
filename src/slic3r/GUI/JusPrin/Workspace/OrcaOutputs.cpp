// What a project produces and shows: an offscreen picture of a plate (the
// thumbnail renderer), its packed attachments, a current slice's layers and
// G-code, the files export writes through Orca's own writers, and stopping a
// run the tool system started.

#include "OrcaWorkspaceAdapter.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/thread.h>

#include <algorithm>
#include <map>
#include <set>

namespace Slic3r::GUI::JusPrin::Workspace {
namespace {

namespace fs = boost::filesystem;

constexpr int         kImageEdge   = 1280;
constexpr std::size_t kImageBytes  = 2 * 1024 * 1024;
constexpr std::size_t kTextBytes   = 32 * 1024;
constexpr std::size_t kGcodeBytes  = 64 * 1024;
constexpr std::size_t kLayerLimit  = 100;

int plate_index_of(PartPlateList& plates, std::optional<PlateId> plate, ProjectSessionId session)
{
    if (!plate)
        return plates.get_curr_plate_index();
    if (plate->session() != session)
        return -1;
    for (int index = 0; index < plates.get_plate_count(); ++index)
        if (plates.get_plate(index)->id().id == plate->value())
            return index;
    return -1;
}

std::optional<Camera::ViewAngleType> view_angle(const std::string& view)
{
    static const std::map<std::string, Camera::ViewAngleType> views{
        {"iso", Camera::ViewAngleType::Iso},       {"front", Camera::ViewAngleType::Front},
        {"rear", Camera::ViewAngleType::Rear},     {"left", Camera::ViewAngleType::Left},
        {"right", Camera::ViewAngleType::Right},   {"top", Camera::ViewAngleType::Top},
        {"bottom", Camera::ViewAngleType::Bottom}, {"top_front", Camera::ViewAngleType::Top_Front},
        {"plate", Camera::ViewAngleType::Top_Plate}};
    const auto found = views.find(view);
    return found == views.end() ? std::nullopt : std::optional<Camera::ViewAngleType>(found->second);
}

std::string lower_extension(const fs::path& path) { return boost::algorithm::to_lower_copy(path.extension().string()); }

// A picture within the caps: as it is when it already fits, otherwise scaled
// to 1280 pixels on its long edge and re-encoded, as JPEG if PNG is still too
// large.
bool bounded_image(const std::string& bytes, const std::string& extension, AttachmentContent& content)
{
    wxMemoryInputStream input(bytes.data(), bytes.size());
    wxImage             image;
    if (!image.LoadFile(input, wxBITMAP_TYPE_ANY) || !image.IsOk())
        return false;
    const bool encodable = extension == ".png" || extension == ".jpg" || extension == ".jpeg";
    if (encodable && bytes.size() <= kImageBytes && std::max(image.GetWidth(), image.GetHeight()) <= kImageEdge) {
        content.mime_type = extension == ".png" ? "image/png" : "image/jpeg";
        content.data      = bytes;
        content.width     = image.GetWidth();
        content.height    = image.GetHeight();
        return true;
    }
    const double scale = std::min(1.0, double(kImageEdge) / std::max(image.GetWidth(), image.GetHeight()));
    if (scale < 1.0)
        image.Rescale(std::max(1, int(image.GetWidth() * scale)), std::max(1, int(image.GetHeight() * scale)), wxIMAGE_QUALITY_HIGH);
    const auto encode = [&image](wxBitmapType type) {
        wxMemoryOutputStream output;
        if (!image.SaveFile(output, type))
            return std::string();
        std::string encoded(output.GetSize(), '\0');
        output.CopyTo(encoded.data(), encoded.size());
        return encoded;
    };
    std::string encoded = encode(wxBITMAP_TYPE_PNG);
    content.mime_type   = "image/png";
    if (encoded.empty() || encoded.size() > kImageBytes) {
        image.SetOption(wxIMAGE_OPTION_QUALITY, 85);
        encoded           = encode(wxBITMAP_TYPE_JPEG);
        content.mime_type = "image/jpeg";
    }
    if (encoded.empty() || encoded.size() > kImageBytes)
        return false;
    content.data      = std::move(encoded);
    content.width     = image.GetWidth();
    content.height    = image.GetHeight();
    content.truncated = true;
    return true;
}

// Text cut at a character boundary.
std::string bounded_text(std::string text, std::size_t limit, bool& truncated)
{
    if (text.size() <= limit)
        return text;
    truncated = true;
    text.resize(limit);
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
        text.pop_back();
    if (!text.empty() && (static_cast<unsigned char>(text.back()) & 0x80))
        text.pop_back();
    return text;
}

bool inside(const fs::path& path, const fs::path& root)
{
    if (root.empty())
        return false;
    boost::system::error_code error;
    const fs::path canonical_root = fs::weakly_canonical(root, error);
    const fs::path canonical_path = fs::weakly_canonical(path, error);
    const std::string r = canonical_root.generic_string(), p = canonical_path.generic_string();
    return p.size() >= r.size() && boost::algorithm::to_lower_copy(p.substr(0, r.size())) == boost::algorithm::to_lower_copy(r);
}

} // namespace

CommandResult OrcaWorkspaceAdapter::render_view(const RenderRequest& request, RenderedImage& image)
{
    wxASSERT(wxIsMainThread());
    GLCanvas3D* canvas = m_plater.get_view3D_canvas3D();
    if (canvas == nullptr || !canvas->is_initialized())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The 3D view is not ready to render yet");
    PartPlateList& plates = m_plater.get_partplate_list();
    const int      index  = plate_index_of(plates, request.plate, m_session);
    if (index < 0)
        return CommandResult::failure(WorkspaceError::StaleId, "That plate is not in the open project");
    const auto angle = view_angle(request.view);
    if (!angle)
        return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + request.view + "\" is not a view");
    const unsigned width  = unsigned(std::clamp(request.width, 64, kImageEdge));
    const unsigned height = unsigned(std::clamp(request.height, 64, kImageEdge));
    // The main 3MF thumbnail's parameters: the plate's printable parts, lit.
    ThumbnailData    data;
    ThumbnailsParams params{{}, false, true, false, true, index};
    canvas->render_thumbnail(data, width, height, params, Camera::EType::Ortho, *angle);
    if (!data.is_valid())
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer rendered nothing");
    const auto png = GCodeThumbnails::compress_thumbnail(data, GCodeThumbnailsFormat::PNG);
    if (!png || png->size == 0 || png->size > kImageBytes)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The rendered picture could not be encoded");
    image.plate  = PlateId(m_session, plates.get_plate(index)->id().id);
    image.view   = request.view;
    image.width  = int(data.width);
    image.height = int(data.height);
    image.png.assign(static_cast<const char*>(png->data), png->size);
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::read_attachment(const std::string& id, AttachmentContent& content) const
{
    wxASSERT(wxIsMainThread());
    // Only a file the project lists: a relative path inside the attachment
    // folder, outside JusPrin's own state and Orca's thumbnail cache.
    const fs::path root(auxiliary_data_dir());
    const fs::path relative(id);
    const std::string first = relative.empty() ? std::string() : relative.begin()->generic_string();
    bool listed = !id.empty() && !relative.is_absolute() && first != "JusPrin" && first != ".thumbnails";
    for (const auto& part : relative)
        listed = listed && part != ".." && part != ".";
    boost::system::error_code error;
    const fs::path path = root / relative;
    if (!listed || !fs::is_regular_file(path, error) || !inside(path, root))
        return CommandResult::failure(WorkspaceError::InvalidArgument,
                                      "The project has no attachment " + id + "; read workspace_inspect's project section");
    content.id     = relative.generic_string();
    content.folder = first;
    content.bytes  = fs::file_size(path, error);
    const std::string extension = lower_extension(path);
    const std::set<std::string> images{".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".tif", ".tiff"};
    const std::set<std::string> texts{".txt", ".md", ".csv", ".json", ".xml", ".ini", ".cfg", ".log", ".gcode", ".html", ".htm"};
    if (!images.count(extension) && !texts.count(extension)) {
        content.kind      = "other";
        content.mime_type = extension == ".pdf" ? "application/pdf" : "application/octet-stream";
        return CommandResult::success();
    }
    // Images are read whole (a picture over 64 MB is not an attachment worth
    // decoding); text only as far as the cap.
    if (images.count(extension) && content.bytes > 64 * 1024 * 1024)
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "That picture is too large to read");
    boost::nowide::ifstream file(path.string(), std::ios::binary);
    std::string bytes;
    if (images.count(extension)) {
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        content.kind = "image";
        if (!bounded_image(bytes, extension, content))
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer could not read that picture");
        return CommandResult::success();
    }
    bytes.resize(std::min<std::uint64_t>(content.bytes, kTextBytes + 4));
    file.read(bytes.data(), std::streamsize(bytes.size()));
    bytes.resize(std::size_t(file.gcount()));
    content.kind      = "text";
    content.mime_type = extension == ".json" ? "application/json" : extension == ".md" ? "text/markdown" : "text/plain";
    content.data      = bounded_text(std::move(bytes), kTextBytes, content.truncated);
    content.truncated = content.truncated || content.bytes > kTextBytes;
    return CommandResult::success();
}

SliceInspection OrcaWorkspaceAdapter::inspect_slice(const SliceInspectRequest& request) const
{
    wxASSERT(wxIsMainThread());
    SliceInspection inspection;
    PartPlateList& plates = m_plater.get_partplate_list();
    const int      index  = plate_index_of(plates, request.plate, m_session);
    if (index < 0)
        return inspection;
    PartPlate* plate = plates.get_plate(index);
    if (!plate->is_slice_result_valid() || m_plater.is_background_process_slicing() || plate->get_slice_result() == nullptr)
        return inspection;
    const GCodeProcessorResult& result = *plate->get_slice_result();
    inspection.valid = true;

    if (request.gcode) {
        const std::string source = !result.filename.empty() ? result.filename : plate->get_tmp_gcode_path();
        boost::nowide::ifstream file(source, std::ios::binary);
        std::string line;
        std::size_t number = 0;
        while (number < request.first && std::getline(file, line))
            ++number;
        inspection.first_line = number;
        while (std::getline(file, line)) {
            if (inspection.gcode.size() + line.size() + 1 > kGcodeBytes) {
                inspection.next = number;
                break;
            }
            inspection.gcode += line;
            inspection.gcode += '\n';
            ++number;
        }
        return inspection;
    }

    // The preview's own layer split: a new layer at each new layer id, its
    // height the last extrusion's, its time the moves' times summed.
    std::vector<SliceLayer> layers;
    std::vector<std::set<std::string>> roles;
    const auto normal = std::size_t(PrintEstimatedStatistics::ETimeMode::Normal);
    for (const GCodeProcessorResult::MoveVertex& move : result.moves) {
        if (move.layer_id >= layers.size()) {
            layers.resize(move.layer_id + 1);
            roles.resize(move.layer_id + 1);
        }
        SliceLayer& layer = layers[move.layer_id];
        layer.index       = move.layer_id;
        layer.seconds += move.time[normal];
        if (move.type != EMoveType::Extrude || move.extrusion_role == erCustom)
            continue;
        const bool first = roles[move.layer_id].empty();
        roles[move.layer_id].insert(ExtrusionEntity::role_to_string(move.extrusion_role));
        const auto widen = [first](double& low, double& high, double value) {
            low  = first ? value : std::min(low, value);
            high = first ? value : std::max(high, value);
        };
        layer.z      = move.position.z();
        layer.height = move.height;
        widen(layer.speed_min, layer.speed_max, move.feedrate);
        widen(layer.fan_min, layer.fan_max, move.fan_speed);
        widen(layer.temperature_min, layer.temperature_max, move.temperature);
        widen(layer.flow_min, layer.flow_max, move.volumetric_rate());
    }
    inspection.layer_count = layers.size();
    const std::size_t count = std::min(request.count, kLayerLimit);
    for (std::size_t index = request.first; index < layers.size() && index < request.first + count; ++index) {
        layers[index].roles.assign(roles[index].begin(), roles[index].end());
        inspection.layers.push_back(layers[index]);
    }
    if (request.first + count < layers.size())
        inspection.next = request.first + count;
    return inspection;
}

CommandResult OrcaWorkspaceAdapter::check_export(const ExportRequest& request) const
{
    wxASSERT(wxIsMainThread());
    const fs::path path = fs::path(request.path);
    boost::system::error_code error;
    if (!path.is_absolute())
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The export path must be absolute");
    const bool folder = request.kind == "presets";
    const fs::path directory = folder ? path : path.parent_path();
    if (!fs::is_directory(directory, error))
        return CommandResult::failure(WorkspaceError::InvalidArgument, "The folder " + directory.generic_string() + " does not exist");
    // The app's own folders are not export destinations.
    if (inside(path, fs::path(data_dir())) || inside(path, fs::path(auxiliary_data_dir())))
        return CommandResult::failure(WorkspaceError::InvalidArgument, "OrcaSlicer's own data folders are not an export destination");
    const std::string extension = lower_extension(path);
    const std::map<std::string, std::string> wanted{{"gcode", ".gcode"}, {"sliced_3mf", ".3mf"}, {"project_3mf", ".3mf"}, {"stl", ".stl"}};
    if (!folder) {
        const auto expected = wanted.find(request.kind);
        if (expected == wanted.end())
            return CommandResult::failure(WorkspaceError::InvalidArgument, "\"" + request.kind + "\" is not an export kind");
        if (extension != expected->second)
            return CommandResult::failure(WorkspaceError::InvalidArgument, "A " + request.kind + " file ends in " + expected->second);
        if (fs::exists(path, error) && !request.overwrite)
            return CommandResult::failure(WorkspaceError::InvalidArgument, path.generic_string() + " exists; pass overwrite to replace it");
        if (fs::is_directory(path, error))
            return CommandResult::failure(WorkspaceError::InvalidArgument, path.generic_string() + " is a folder");
    }
    if (request.kind == "gcode" || request.kind == "sliced_3mf") {
        PartPlateList& plates = m_plater.get_partplate_list();
        const int      index  = plate_index_of(plates, request.plate, m_session);
        if (index < 0)
            return CommandResult::failure(WorkspaceError::StaleId, "That plate is not in the open project");
        PartPlate* plate = plates.get_plate(index);
        if (!plate->is_slice_result_valid() || plate->get_slice_result() == nullptr || m_plater.is_background_process_slicing())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "Slice the plate first; it has no current G-code");
    }
    if (request.kind == "stl") {
        for (ObjectId id : request.objects)
            if (!resolve(id))
                return id_error(id);
        if (request.objects.empty() && plate_index_of(m_plater.get_partplate_list(), request.plate, m_session) < 0)
            return CommandResult::failure(WorkspaceError::StaleId, "That plate is not in the open project");
    }
    if ((request.kind == "sliced_3mf" || request.kind == "project_3mf") &&
        (m_plater.get_view3D_canvas3D() == nullptr || !m_plater.get_view3D_canvas3D()->is_initialized()))
        return CommandResult::failure(WorkspaceError::UnavailableOperation, "The 3D view is not ready to render the file's thumbnails yet");
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::export_file(const ExportRequest& request, ExportResult& result)
{
    wxASSERT(wxIsMainThread());
    if (CommandResult checked = check_export(request); !checked.succeeded())
        return checked;
    const fs::path path = fs::path(request.path);
    PartPlateList& plates = m_plater.get_partplate_list();
    const int      index  = plate_index_of(plates, request.plate, m_session);
    result = {};
    if (request.kind == "gcode") {
        // The plate's G-code as the slicer wrote it; Orca's export would add
        // only its post-processing scripts and the removable-media handling.
        const GCodeProcessorResult& sliced = *plates.get_plate(index)->get_slice_result();
        const std::string source = !sliced.filename.empty() ? sliced.filename : plates.get_plate(index)->get_tmp_gcode_path();
        std::string error;
        if (copy_file(source, path.string(), error, false) != CopyFileResult::SUCCESS)
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "The G-code could not be written: " + error);
    } else if (request.kind == "sliced_3mf" || request.kind == "project_3mf") {
        // export_gcode_3mf's strategy for a sliced file; an ordinary save's
        // for a project. Silence keeps the open project's name and path.
        const SaveStrategy strategy = request.kind == "sliced_3mf" ?
                                          SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::WithGcode | SaveStrategy::SkipModel :
                                          SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh;
        if (m_plater.export_3mf(path, strategy, request.kind == "sliced_3mf" ? index : -1) < 0)
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "OrcaSlicer could not write the file");
    } else if (request.kind == "stl") {
        // The model parts of the chosen objects, or of the plate's objects,
        // as they are placed.
        TriangleMesh mesh;
        std::vector<const ModelObject*> objects;
        for (ObjectId id : request.objects)
            objects.push_back(m_plater.model().objects[resolve(id)->index]);
        if (request.objects.empty())
            for (std::size_t object = 0; object < m_plater.model().objects.size(); ++object)
                if (plates.find_instance_belongs(int(object), 0) == index)
                    objects.push_back(m_plater.model().objects[object]);
        if (objects.empty())
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "There is nothing to export on that plate");
        for (const ModelObject* object : objects)
            for (const ModelInstance* instance : object->instances) {
                TriangleMesh part = object->raw_mesh();
                part.transform(instance->get_matrix());
                mesh.merge(part);
            }
        if (!store_stl(path.string().c_str(), &mesh, true))
            return CommandResult::failure(WorkspaceError::UnavailableOperation, "The STL could not be written");
    } else {
        // Orca's export of the presets in use, as edited, system ones
        // included (Orca's menu leaves those out, and in use they usually
        // are system presets with edits); an existing file is replaced only
        // when the call said so.
        result.files = wxGetApp().preset_bundle->export_current_configs(
            path.string(), [&request](const std::string&) { return request.overwrite ? 3 : 2; }, true, true);
        if (result.files.empty())
            return CommandResult::failure(WorkspaceError::InvalidArgument,
                                          "Nothing was written; the preset files exist in that folder (pass overwrite to replace them)");
        boost::system::error_code error;
        for (const std::string& file : result.files)
            result.bytes += fs::file_size(fs::path(file), error);
        return CommandResult::success();
    }
    boost::system::error_code error;
    result.files = {path.string()};
    result.bytes = fs::file_size(path, error);
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::cancel_slice(bool& stopped)
{
    wxASSERT(wxIsMainThread());
    // The slicing notification's own Cancel: it returns once the run stopped.
    stopped = m_plater.cancel_slicing();
    return CommandResult::success();
}

CommandResult OrcaWorkspaceAdapter::cancel_job(const std::string& handle, bool& stopped)
{
    wxASSERT(wxIsMainThread());
    const auto job = std::find_if(m_jobs.begin(), m_jobs.end(), [&handle](const WorkspaceJob& j) { return j.handle == handle; });
    if (job == m_jobs.end())
        return CommandResult::failure(WorkspaceError::InvalidArgument, "No job has the handle " + handle);
    stopped = job->state == "running";
    // The worker's cancel only asks; the job's own end reports whether it
    // stopped, in the jobs list.
    if (stopped)
        m_plater.get_ui_job_worker().cancel();
    return CommandResult::success();
}

} // namespace Slic3r::GUI::JusPrin::Workspace
