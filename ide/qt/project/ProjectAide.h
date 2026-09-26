#pragma once

// Internal helper shared by Project.cpp and every other adapter that needs
// to hand a Project's data to a core/ function (MakefileGenerator.cpp,
// VcConfigGenerator.cpp, BuildManager.cpp, IconWriter.cpp, ...): converts
// between the Qt-facing Project and the portable AideProject C struct
// (core/include/aide/aide_project.h). Not part of the public Project API.

#include "Project.h"
#include "aide/aide_project.h"

// Fills an aide_project_init()'d-or-not *a from every field of `p`
// (calls aide_project_init() itself first).
void projectToAide(const Project &p, AideProject *a);

// Fills `p`'s public fields from *a. Does not touch p's filePath/raw JSON.
void aideToProject(const AideProject &a, Project *p);
