#ifndef AWS_IOT_JOBS_HANDLER_H
#define AWS_IOT_JOBS_HANDLER_H

#include "DeviceShadowState.h"
#include "cJSON.h"
#include <string>
#include <functional>
#include <vector>
#include <optional>

// Forward declaration
class AwsIotClient;

/**
 * @brief Handler for AWS IoT Jobs and FOTA updates
 */
class AwsIotJobsHandler {
public:
    using JobCallback = std::function<void(const AwsIotJob&)>;
    using FotaProgressCallback = std::function<void(size_t downloaded, size_t total)>;
    
    AwsIotJobsHandler(AwsIotClient* client);
    ~AwsIotJobsHandler();
    
    /**
     * @brief Initialize jobs handler and subscribe to job topics
     */
    bool Initialize(const std::string& thingName);
    
    /**
     * @brief Request pending jobs from AWS IoT
     */
    bool RequestPendingJobs();
    
    /**
     * @brief Get the next job from the queue
     */
    bool GetNextJob(AwsIotJob& job);
    
    /**
     * @brief Update job execution status
     */
    bool UpdateJobStatus(const std::string& jobId, JobExecutionStatus status, 
                        const std::string& statusDetails = "");
    
    /**
     * @brief Process a FOTA job
     */
    bool ProcessFotaJob(const AwsIotJob& job);
    
    /**
     * @brief Parse job document from JSON
     */
    static bool ParseJobDocument(const char* json, AwsIotJob& job);
    
    /**
     * @brief Parse job execution from notify-next response
     */
    static bool ParseJobExecution(const char* json, AwsIotJob& job);
    
    /**
     * @brief Create job status update JSON
     */
    std::string CreateJobStatusUpdate(JobExecutionStatus status, 
                                  const std::string& statusDetails);
    
    /**
     * @brief Set callback for new jobs
     */
    void SetJobCallback(JobCallback callback) { job_callback = callback; }
    
    /**
     * @brief Set callback for FOTA progress
     */
    void SetFotaProgressCallback(FotaProgressCallback callback) { 
        fota_progress_callback = callback; 
    }
    
    /**
     * @brief Check if a FOTA update is available
     */
    bool IsFotaUpdateAvailable() const { return !pending_jobs.empty(); }
    
    /**
     * @brief Get current job being executed
     */
    const AwsIotJob* GetCurrentJob() const { 
        return current_job ? &(*current_job) : nullptr; 
    }

    /**
     * @brief Handle incoming MQTT message on a job topic.
     * Called by AwsIotClient's generic message callback.
     */
    void handleJobNotification(const std::string& topic, const std::string& payload);

private:
    AwsIotClient* aws_client;
    std::string thing_name;
    std::vector<AwsIotJob> pending_jobs;
    std::optional<AwsIotJob> current_job;

    JobCallback job_callback;
    FotaProgressCallback fota_progress_callback;
    
    /**
     * @brief Download firmware from URL
     */
    bool downloadFirmware(const std::string& url, const std::string& md5, 
                         uint32_t size);
    
    /**
     * @brief Verify firmware integrity
     */
    bool verifyFirmware(const uint8_t* data, size_t size, const std::string& md5);
    
    /**
     * @brief Apply firmware update (platform specific)
     */
    bool applyFirmwareUpdate(const uint8_t* data, size_t size);
    
    static const char* TAG;
};

// Job topic definitions
#define AWS_IOT_JOBS_NOTIFY_TOPIC           "$aws/things/%s/jobs/notify"
#define AWS_IOT_JOBS_NOTIFY_NEXT_TOPIC      "$aws/things/%s/jobs/notify-next"
#define AWS_IOT_JOBS_GET_TOPIC              "$aws/things/%s/jobs/get"
#define AWS_IOT_JOBS_GET_ACCEPTED_TOPIC     "$aws/things/%s/jobs/get/accepted"
#define AWS_IOT_JOBS_GET_REJECTED_TOPIC     "$aws/things/%s/jobs/get/rejected"
#define AWS_IOT_JOBS_START_NEXT_TOPIC       "$aws/things/%s/jobs/start-next"
#define AWS_IOT_JOBS_START_NEXT_ACCEPTED    "$aws/things/%s/jobs/start-next/accepted"
#define AWS_IOT_JOBS_START_NEXT_REJECTED    "$aws/things/%s/jobs/start-next/rejected"
#define AWS_IOT_JOBS_UPDATE_TOPIC           "$aws/things/%s/jobs/%s/update"
#define AWS_IOT_JOBS_UPDATE_ACCEPTED        "$aws/things/%s/jobs/%s/update/accepted"
#define AWS_IOT_JOBS_UPDATE_REJECTED        "$aws/things/%s/jobs/%s/update/rejected"

#endif // AWS_IOT_JOBS_HANDLER_H