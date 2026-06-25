#ifndef DEVICE_SHADOW_STATE_H_
#define DEVICE_SHADOW_STATE_H_

#include <string>
#include <cstdint>

// Define a constant for the maximum number of schedule entries.
static const int MAX_SCHEDULE_ENTRIES = 8;

// Main device shadow state structure
struct DeviceShadowState {
    // --- Updated field using a fixed-size array ---
    std::string post_schedule[MAX_SCHEDULE_ENTRIES];

    // --- Other fields remain the same ---
    uint32_t fill_dwell_time = 0; // Units: seconds. REV B min 300.
    uint32_t lte_timeout = 0;     // Units: seconds. REV B renames to wifi_timeout; same field, min 15.
    uint8_t fill_alarm_delta = 0; // Units: percent. REV B min 10.
    uint32_t post_dwell_time = 0; // Units: seconds. REV B min 60.
    std::string mqtt_scheduled_post;
    bool event_posts = false;
    bool event_posts_valid = false;
    std::string event_thresholds_pct;
    std::string event_direction;
    uint32_t sensor_sample_rate = 0;
    bool acquire_gps = false;
    bool acquire_gps_valid = false;
    uint32_t mqtt_timeout = 0;

    // Provisioning reset trigger (cloud-initiated)
    bool reset_provisioning = false;

    // Note: post_hour and post_minute are now managed by post_schedule
    int post_hour = 0;
    int post_minute = 0;
    bool enabled = false;
    
    // FOTA configuration
    bool fota_enabled = true;
    std::string firmware_version = "1.0.0";
    
    // Device status
    uint32_t last_post_timestamp = 0;
    int rssi = 0;
    uint32_t uptime_seconds = 0;
    uint32_t free_heap = 0;
    
    // Additional configuration
    uint32_t telemetry_interval_seconds = 3600;
    uint32_t heartbeat_interval_seconds = 300;
    bool debug_mode = false;
};

// Structure for AWS IoT Job document
struct AwsIotJob {
    std::string job_id;
    std::string job_document;
    std::string status;  // QUEUED, IN_PROGRESS, SUCCEEDED, FAILED
    int64_t queued_at = 0;
    int64_t started_at = 0;
    int64_t version_number = 0;
    
    // FOTA specific fields
    std::string firmware_url;
    std::string firmware_version;
    std::string firmware_md5;
    uint32_t firmware_size = 0;
    bool force_update = false;
};

// Job execution status
enum class JobExecutionStatus {
    QUEUED,
    IN_PROGRESS,
    SUCCEEDED,
    FAILED,
    REJECTED,
    REMOVED,
    CANCELED
};

// Convert enum to string for AWS IoT
inline const char* jobStatusToString(JobExecutionStatus status) {
    switch(status) {
        case JobExecutionStatus::QUEUED: return "QUEUED";
        case JobExecutionStatus::IN_PROGRESS: return "IN_PROGRESS";
        case JobExecutionStatus::SUCCEEDED: return "SUCCEEDED";
        case JobExecutionStatus::FAILED: return "FAILED";
        case JobExecutionStatus::REJECTED: return "REJECTED";
        case JobExecutionStatus::REMOVED: return "REMOVED";
        case JobExecutionStatus::CANCELED: return "CANCELED";
        default: return "UNKNOWN";
    }
}

#endif // DEVICE_SHADOW_STATE_H_
