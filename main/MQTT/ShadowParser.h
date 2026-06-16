#ifndef SHADOW_PARSER_H
#define SHADOW_PARSER_H

#include "DeviceShadowState.h"
#include "LogiSensorData.h"
#include "CLibFiles/DateTime/WeeklySchedule.h"
#include <string>

/**
 * @brief Parse enhanced shadow document with nested structure support
 * @param payload JSON payload from AWS IoT
 * @param stateOut Parsed shadow state
 * @return true if parsing successful
 */
bool ParseEnhancedShadowDocument(const char* payload, DeviceShadowState& stateOut);

/**
 * @brief Create comprehensive shadow update document
 * @param state Current device shadow state
 * @param sensorData Optional sensor data to include
 * @return JSON string for shadow update
 */
std::string CreateEnhancedShadowUpdate(const DeviceShadowState& state, 
                                       const LogiSensorData* sensorData = nullptr);

/**
 * @brief Merge delta updates into current state
 * @param currentState Current state to update
 * @param deltaState Delta changes to apply
 */
void MergeShadowDelta(DeviceShadowState& currentState, const DeviceShadowState& deltaState);

/**
 * @brief Convert shadow post_schedule strings to WeeklySchedule array
 * @param shadowState Shadow state containing post_schedule strings
 * @param schedulesOut Output array of WeeklySchedule (must be size MAX_SCHEDULE_ENTRIES)
 * @return Number of valid schedules parsed
 *
 * Shadow format: "HH:MM;XX" where XX is hex with bit 7=enable, bits 0-6=day bitmask
 */
int ConvertShadowSchedulesToWeekly(const DeviceShadowState& shadowState,
                                    WeeklySchedule schedulesOut[MAX_SCHEDULE_ENTRIES]);

#endif // SHADOW_PARSER_H