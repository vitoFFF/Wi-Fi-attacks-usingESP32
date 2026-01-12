/**
 * @file attack_beacon.h
 * @brief SSID Beacon Swarm Attack - broadcasts multiple fake SSIDs
 */
#ifndef ATTACK_BEACON_H
#define ATTACK_BEACON_H

#include "attack.h"

/**
 * @brief Starts the beacon swarm attack.
 * Broadcasts multiple fake SSIDs to flood WiFi scanners.
 * @param attack_config Attack configuration with timeout
 */
void attack_beacon_start(attack_config_t *attack_config);

/**
 * @brief Stops the beacon swarm attack and cleans up resources.
 */
void attack_beacon_stop(void);

#endif
