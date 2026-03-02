#ifndef HOMEKIT_BRIDGE_H
#define HOMEKIT_BRIDGE_H

#include "config.h"
#include "state.h"
#include <string>

void homekitBridgeSetup(const Configuration &config);
void homekitBridgeLoop();
void homekitBridgeSync(const SystemState &state);
bool homekitBridgeEnabled(const Configuration &config);
std::string homekitBridgeGetSetupUri(const Configuration &config);
std::string homekitBridgeGetQrCodeUrl(const Configuration &config);
std::string homekitBridgeGetStatusJson(const Configuration &config,
                                       const SystemState &state,
                                       const std::string &ipAddress);

#endif // HOMEKIT_BRIDGE_H
