#pragma once

// Neutral forwarding header. The JusPrin project-state seam lives under
// src/slic3r/GUI/JusPrin/Workspace/; this shim lets Orca-owned files include
// "ProjectState.hpp" without naming a JusPrin/... path. Do not add declarations
// here — put them in the Workspace headers below.

#include "JusPrin/Workspace/ProjectState.hpp"
#include "JusPrin/Workspace/PlaterProjectState.hpp"
