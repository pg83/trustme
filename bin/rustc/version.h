#pragma once

#include <string>

extern unsigned int giVersionMajor;
extern unsigned int giVersionMinor;
extern unsigned int giVersionPatch;
extern const char* gsVersionGitHash;
extern const char* gsVersionGitShortHash;
extern const char* gsVersionBuildTime;
extern bool gbVersionGitDirty;

extern ::std::string VersionGetString();
