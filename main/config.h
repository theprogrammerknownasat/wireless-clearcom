/**
* @file config.h
 * @brief Master Configuration Selector
 *
 * This file determines which device type to build.
 * Change ONE line to switch between base station and belt pack.
 */

#ifndef CONFIG_H
#define CONFIG_H

//=============================================================================
// DEVICE TYPE SELECTION
//=============================================================================
// Uncomment EXACTLY ONE of the following:

//#define BUILD_BASE_STATION
#define BUILD_BELT_PACK

//=============================================================================
// Automatic Configuration Loading
//=============================================================================

#include "config_common.h"

#ifdef BUILD_BASE_STATION
    #include "config_base.h"
    #define DEVICE_TYPE_STRING "BASE STATION"
    #define DEVICE_TYPE_BASE 1
    #define DEVICE_TYPE_PACK 0

#elif defined(BUILD_BELT_PACK)
    #include "config_pack.h"
    #define DEVICE_TYPE_STRING "BELT PACK"
    #define DEVICE_TYPE_BASE 0
    #define DEVICE_TYPE_PACK 1

#else
    #error "ERROR: Must define BUILD_BASE_STATION or BUILD_BELT_PACK in config.h"
#endif

//=============================================================================
// Build Information
//=============================================================================

#define FIRMWARE_VERSION "1.0.0"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

#endif // CONFIG_H