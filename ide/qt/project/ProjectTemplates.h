#pragma once

#include "Project.h"

// Starter files for a new project.
//
// New Project deliberately creates an empty project -- files come from
// "Add New File to Project" -- and that is right for a program, where an
// empty main.c is a fine starting point. It is not right for a shared
// library: its romtag, init table and function-vector table are
// boilerplate that has to be present and correct before anything links at
// all, and the vector order defines the LVOs, so it cannot be generated
// after the fact from the sources.
//
// The files are embedded as Qt resources (templates/templates.qrc), so
// they travel with the binary rather than depending on an install path.
namespace ProjectTemplates {

// Writes the starter files for `project`'s output kind into its directory
// and adds them to project->sources. Existing files are never overwritten.
// Does nothing when the kind has no template, or when the project already
// has sources. Returns the relative paths actually created.
QStringList apply(Project *project);

}
