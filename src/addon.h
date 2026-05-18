#pragma once
#include "Nexus.h"
#include <string>

extern AddonAPI_t* APIDefs;
extern bool IsMapOpen;

void Log(ELogLevel level, const char* msg);
void LogInfo(const char* msg);
void LogWarn(const char* msg);
void LogDebug(const char* msg);

enum class FetchMode { OnDemand, CreateDBSequential, CreateDBParallel };

void SaveSettingsApiKey(const std::string& key);
std::string LoadSettingsApiKey();
struct SearchOptions;
void SaveSettingsOptions(bool hideLegendaryArmory, bool hideEquippedBags, bool autoFocus);
void LoadSettingsOptions(bool& hideLegendaryArmory, bool& hideEquippedBags, bool& autoFocus);
void SaveFetchMode(FetchMode mode);
FetchMode LoadFetchMode();
