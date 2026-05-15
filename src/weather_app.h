#pragma once

void weatherAppBegin();
void weatherAppStart();
void weatherAppUpdate();
void weatherAppRefresh();
void weatherAppStop();
bool weatherAppWifiConnected();
bool weatherAppApplyRemoteConfig(const char* locationName, const char* latitude,
                                 const char* longitude, const char* timezone);
