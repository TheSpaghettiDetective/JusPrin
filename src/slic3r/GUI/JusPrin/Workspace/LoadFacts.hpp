#pragma once

// Facts the file-load report gives the Agent beside what Orca said: whether
// an object fits a printer's bed, and what a 3MF states about the printer and
// filaments it was made for. GUI-free and Orca-free, so both test against
// plain values; the adapter reads the configs and the archive.

#include <string>
#include <vector>

namespace Slic3r::GUI::JusPrin::Workspace {

struct BedPoint
{
    double x{0}, y{0};
};

// A printer's bed as Orca's plate reads it: the printable_area outline,
// bed_exclude_area as the profile writes it, and printable_height.
struct BedShape
{
    std::vector<BedPoint> area;
    std::vector<BedPoint> excluded;
    double                height{0};
};

// Width and depth of the printable area's bounding box, and the printable
// height, in mm.
struct BedSize
{
    double width{0}, depth{0}, height{0};
};

BedSize bed_size(const BedShape& bed);

// Whether an object can stand on the bed somewhere, as placed or turned a
// quarter-turn, by the test Orca's plate makes of a placed object
// (PartPlate::check_outside): its bounding box inside the printable area, its
// outline (the footprint, a convex hull at any position) clear of every
// excluded area, and its height within the printable height. Excluded areas
// are read as the plate reads them: every four points are one box, the
// bounding box of those points, and a remainder of fewer than four points is
// ignored.
bool fits_bed(const BedShape& bed, const std::vector<BedPoint>& footprint, double height);
// The same for a rectangular footprint.
bool fits_bed(const BedShape& bed, double width, double depth, double height);

// What a 3MF states about itself, from its own metadata and settings.
struct LoadedFileFacts
{
    struct Filament
    {
        std::string type;
        std::string colour; // as the file writes it, e.g. "#FF8000"
    };
    std::string           file;        // the file's name, without its folder
    std::string           application; // the app that saved it, e.g. "BambuStudio-01.10.01.50"
    std::string           printer_model;
    double                nozzle_mm{0};
    std::vector<Filament> filaments;

    bool empty() const { return application.empty() && printer_model.empty() && nozzle_mm == 0 && filaments.empty(); }
};

// The value of the model file's "Application" metadata, from the start of
// 3D/3dmodel.model; empty when absent.
std::string application_from_model_head(const std::string& head);
// Bambu and Orca projects: Metadata/project_settings.config, a JSON object.
// Invalid JSON adds nothing.
void read_project_settings(const std::string& json_text, LoadedFileFacts& facts);
// PrusaSlicer projects: Metadata/Slic3r_PE.config, "; key = value" lines.
void read_prusa_config(const std::string& ini_text, LoadedFileFacts& facts);

} // namespace Slic3r::GUI::JusPrin::Workspace
