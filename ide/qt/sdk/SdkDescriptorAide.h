#pragma once

// Internal helper shared by SdkDescriptor.cpp and SdkRegistry.cpp: converts
// between the Qt-facing SdkDescriptor and the portable AideSdkDescriptor C
// struct (core/include/aide/aide_sdk.h). Not part of the public SdkDescriptor
// API -- only included by the two .cpp files in this directory.

#include "SdkDescriptor.h"
#include "aide/aide_sdk.h"

void sdkDescriptorToAide(const SdkDescriptor &d, AideSdkDescriptor *a);
SdkDescriptor sdkDescriptorFromAide(const AideSdkDescriptor &a);
