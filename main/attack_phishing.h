/**
 * @file attack_phishing.h
 * @brief Captive Portal Phishing Attack - clones AP and serves login page
 */
#ifndef ATTACK_PHISHING_H
#define ATTACK_PHISHING_H

#include "attack.h"

/**
 * @brief Starts captive portal phishing attack.
 * Clones target AP, deauths clients, and serves a fake login page.
 * @param attack_config Attack configuration with target AP
 */
void attack_phishing_start(attack_config_t *attack_config);

/**
 * @brief Stops the phishing attack and restores management AP.
 */
void attack_phishing_stop(void);

#endif
