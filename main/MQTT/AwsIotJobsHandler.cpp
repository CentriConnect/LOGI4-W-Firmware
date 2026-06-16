#include "AwsIotJobsHandler.h"
#include "AwsIotClient.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "mbedtls/md5.h"
#include <cstring>
#include <optional>

const char* AwsIotJobsHandler::TAG = "AwsIotJobsHandler";

AwsIotJobsHandler::AwsIotJobsHandler(AwsIotClient* client)
    : aws_client(client) {
}

AwsIotJobsHandler::~AwsIotJobsHandler() {
}

bool AwsIotJobsHandler::Initialize(const std::string& thingName) 
{
    thing_name = thingName;
    
    if (!aws_client || !aws_client->IsConnected()) {
        ESP_LOGE(TAG, "AWS IoT client not connected");
        return false;
    }
    
    ESP_LOGI(TAG, "Initializing Jobs handler for thing: %s", thing_name.c_str());
    
    // Subscribe to job topics
    char topic[256];
    
    // Subscribe to notify-next for getting pending jobs
    snprintf(topic, sizeof(topic), AWS_IOT_JOBS_NOTIFY_NEXT_TOPIC, thing_name.c_str());
    if (!aws_client->subscribeToTopic(topic, 1)) {
        ESP_LOGE(TAG, "Failed to subscribe to notify-next topic");
        return false;
    }
    
    // Subscribe to get accepted/rejected
    snprintf(topic, sizeof(topic), AWS_IOT_JOBS_GET_ACCEPTED_TOPIC, thing_name.c_str());
    aws_client->subscribeToTopic(topic, 1);
    
    snprintf(topic, sizeof(topic), AWS_IOT_JOBS_GET_REJECTED_TOPIC, thing_name.c_str());
    aws_client->subscribeToTopic(topic, 1);
    
    // Subscribe to start-next accepted/rejected
    snprintf(topic, sizeof(topic), AWS_IOT_JOBS_START_NEXT_ACCEPTED, thing_name.c_str());
    aws_client->subscribeToTopic(topic, 1);
    
    snprintf(topic, sizeof(topic), AWS_IOT_JOBS_START_NEXT_REJECTED, thing_name.c_str());
    aws_client->subscribeToTopic(topic, 1);
    
    ESP_LOGI(TAG, "Jobs handler initialized successfully");
    return true;
}

bool AwsIotJobsHandler::RequestPendingJobs() {
    char topic[256];
    snprintf(topic, sizeof(topic), AWS_IOT_JOBS_GET_TOPIC, thing_name.c_str());
    
    ESP_LOGI(TAG, "Requesting pending jobs");
    
    // Request all pending jobs
    const char* request = "{\"includeJobDocument\": true}";
    return aws_client->publishMessage(topic, request, 1);
}

bool AwsIotJobsHandler::GetNextJob(AwsIotJob& job) {
    char topic[256];
    snprintf(topic, sizeof(topic), AWS_IOT_JOBS_START_NEXT_TOPIC, thing_name.c_str());
    
    ESP_LOGI(TAG, "Getting next job");
    
    // Request to start the next job
    const char* request = "{\"statusDetails\": {\"state\": \"ready\"}}";
    
    if (!aws_client->publishMessage(topic, request, 1)) {
        ESP_LOGE(TAG, "Failed to request next job");
        return false;
    }
    
    // The response will come through the MQTT message handler
    // For now, check if we have pending jobs
    if (!pending_jobs.empty()) {
        job = pending_jobs.front();
        pending_jobs.erase(pending_jobs.begin());
        current_job = job;
        return true;
    }
    
    return false;
}

bool AwsIotJobsHandler::UpdateJobStatus(const std::string& jobId, 
                                        JobExecutionStatus status,
                                        const std::string& statusDetails) {
    char topic[256];
    snprintf(topic, sizeof(topic), AWS_IOT_JOBS_UPDATE_TOPIC, 
             thing_name.c_str(), jobId.c_str());
    
    ESP_LOGI(TAG, "Updating job %s status to %s", 
             jobId.c_str(), jobStatusToString(status));
    
    std::string update = CreateJobStatusUpdate(status, statusDetails);
    
    if (!aws_client->publishMessage(topic, update.c_str(), 1)) {
        ESP_LOGE(TAG, "Failed to update job status");
        return false;
    }
    
    // Clear current job if completed
    if (status == JobExecutionStatus::SUCCEEDED || 
        status == JobExecutionStatus::FAILED ||
        status == JobExecutionStatus::REJECTED) {
        current_job.reset();
    }
    
    return true;
}

bool AwsIotJobsHandler::ParseJobDocument(const char* json, AwsIotJob& job) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse job document");
        return false;
    }
    
    bool success = false;
    
    // Parse job execution
    cJSON* execution = cJSON_GetObjectItem(root, "execution");
    if (execution) {
        cJSON* jobId = cJSON_GetObjectItem(execution, "jobId");
        if (jobId && cJSON_IsString(jobId)) {
            job.job_id = jobId->valuestring;
        }
        
        cJSON* status = cJSON_GetObjectItem(execution, "status");
        if (status && cJSON_IsString(status)) {
            job.status = status->valuestring;
        }
        
        cJSON* queuedAt = cJSON_GetObjectItem(execution, "queuedAt");
        if (queuedAt && cJSON_IsNumber(queuedAt)) {
            job.queued_at = (int64_t)queuedAt->valuedouble;
        }
        
        cJSON* versionNumber = cJSON_GetObjectItem(execution, "versionNumber");
        if (versionNumber && cJSON_IsNumber(versionNumber)) {
            job.version_number = (int64_t)versionNumber->valuedouble;
        }
        
        // Parse job document
        cJSON* jobDoc = cJSON_GetObjectItem(execution, "jobDocument");
        if (jobDoc) {
            // Store raw document
            char* docStr = cJSON_PrintUnformatted(jobDoc);
            if (docStr) {
                job.job_document = docStr;
                free(docStr);
            }
            
            // Parse FOTA specific fields
            cJSON* operation = cJSON_GetObjectItem(jobDoc, "operation");
            if (operation && cJSON_IsString(operation)) {

                if (strcmp(operation->valuestring, "firmwareUpdate") == 0) {
                    // Format A: { "operation":"firmwareUpdate", "firmwareUrl":"...", "firmwareVersion":"...", ... }
                    cJSON* firmwareUrl = cJSON_GetObjectItem(jobDoc, "firmwareUrl");
                    if (firmwareUrl && cJSON_IsString(firmwareUrl)) {
                        job.firmware_url = firmwareUrl->valuestring;
                    }

                    cJSON* firmwareVersion = cJSON_GetObjectItem(jobDoc, "firmwareVersion");
                    if (firmwareVersion && cJSON_IsString(firmwareVersion)) {
                        job.firmware_version = firmwareVersion->valuestring;
                    }

                    cJSON* firmwareMd5 = cJSON_GetObjectItem(jobDoc, "firmwareMd5");
                    if (firmwareMd5 && cJSON_IsString(firmwareMd5)) {
                        job.firmware_md5 = firmwareMd5->valuestring;
                    }

                    cJSON* firmwareSize = cJSON_GetObjectItem(jobDoc, "firmwareSize");
                    if (firmwareSize && cJSON_IsNumber(firmwareSize)) {
                        job.firmware_size = (uint32_t)firmwareSize->valueint;
                    }

                    cJSON* forceUpdate = cJSON_GetObjectItem(jobDoc, "forceUpdate");
                    if (forceUpdate && cJSON_IsBool(forceUpdate)) {
                        job.force_update = cJSON_IsTrue(forceUpdate);
                    }

                } else if (strcmp(operation->valuestring, "app_fw_update") == 0) {
                    // Format B (cellular/production): { "operation":"app_fw_update", "fwversion":"...", "size":..., "location":{...} }
                    cJSON* fwversion = cJSON_GetObjectItem(jobDoc, "fwversion");
                    if (fwversion && cJSON_IsString(fwversion)) {
                        job.firmware_version = fwversion->valuestring;
                    }

                    cJSON* size = cJSON_GetObjectItem(jobDoc, "size");
                    if (size && cJSON_IsNumber(size)) {
                        job.firmware_size = (uint32_t)size->valueint;
                    }

                    // Build URL from location object: protocol + "//" + host + "/" + path
                    cJSON* location = cJSON_GetObjectItem(jobDoc, "location");
                    if (location) {
                        cJSON* protocol = cJSON_GetObjectItem(location, "protocol");
                        cJSON* host = cJSON_GetObjectItem(location, "host");
                        cJSON* path = cJSON_GetObjectItem(location, "path");
                        if (protocol && host && path &&
                            cJSON_IsString(protocol) && cJSON_IsString(host) && cJSON_IsString(path)) {
                            job.firmware_url = std::string(protocol->valuestring) + "//" +
                                               host->valuestring + "/" + path->valuestring;
                        }
                    }

                    cJSON* firmwareMd5 = cJSON_GetObjectItem(jobDoc, "md5");
                    if (firmwareMd5 && cJSON_IsString(firmwareMd5)) {
                        job.firmware_md5 = firmwareMd5->valuestring;
                    }
                }
            }
        }
        
        success = true;
    }
    
    cJSON_Delete(root);
    return success;
}

bool AwsIotJobsHandler::ParseJobExecution(const char* json, AwsIotJob& job) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse job execution");
        return false;
    }
    
    bool success = ParseJobDocument(cJSON_PrintUnformatted(root), job);
    
    cJSON_Delete(root);
    return success;
}

std::string AwsIotJobsHandler::CreateJobStatusUpdate(JobExecutionStatus status, const std::string& statusDetails) 
{
    cJSON* root = cJSON_CreateObject();
    
    cJSON_AddStringToObject(root, "status", jobStatusToString(status));
    
    if (!statusDetails.empty()) {
        cJSON* details = cJSON_CreateObject();
        cJSON_AddStringToObject(details, "message", statusDetails.c_str());
        cJSON_AddNumberToObject(details, "timestamp", (double)time(nullptr));
        cJSON_AddItemToObject(root, "statusDetails", details);
    }
    
    // Add options for in-progress status (omit expectedVersion to avoid VersionMismatch)
    if (status == JobExecutionStatus::IN_PROGRESS) {
        cJSON_AddBoolToObject(root, "includeJobDocument", false);
        cJSON_AddBoolToObject(root, "includeJobExecutionState", true);
    }
    
    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    cJSON_Delete(root);
    free(json_str);
    
    return result;
}

void AwsIotJobsHandler::handleJobNotification(const std::string& topic, const std::string& payload)
{
    if (topic.find("/jobs/") == std::string::npos) return;

    ESP_LOGI(TAG, "Job message on topic: %s", topic.c_str());
    ESP_LOGI(TAG, "Payload (%d bytes): %.*s", (int)payload.size(),
             (int)(payload.size() < 512 ? payload.size() : 512), payload.c_str());

    if (topic.find("/start-next/accepted") != std::string::npos ||
        topic.find("/notify-next") != std::string::npos) {
        // These responses contain a full execution with job document
        AwsIotJob job;
        if (ParseJobDocument(payload.c_str(), job) && !job.job_id.empty()) {
            ESP_LOGI(TAG, "Job received: %s (version: %s, url: %s)",
                     job.job_id.c_str(), job.firmware_version.c_str(),
                     job.firmware_url.c_str());
            pending_jobs.push_back(job);
            if (job_callback) job_callback(job);
        }
    }
    else if (topic.find("/get/accepted") != std::string::npos) {
        // get/accepted returns job summaries (no documents) - just log the count
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root) {
            cJSON* queued = cJSON_GetObjectItem(root, "queuedJobs");
            cJSON* inProg = cJSON_GetObjectItem(root, "inProgressJobs");
            int qCount = (queued && cJSON_IsArray(queued)) ? cJSON_GetArraySize(queued) : 0;
            int iCount = (inProg && cJSON_IsArray(inProg)) ? cJSON_GetArraySize(inProg) : 0;
            ESP_LOGI(TAG, "Jobs status: %d queued, %d in-progress", qCount, iCount);
            cJSON_Delete(root);
        }
    }
    else if (topic.find("/get/rejected") != std::string::npos ||
             topic.find("/start-next/rejected") != std::string::npos) {
        ESP_LOGW(TAG, "Job request rejected: %s", payload.c_str());
    }
}

bool AwsIotJobsHandler::ProcessFotaJob(const AwsIotJob& job) 
{
    ESP_LOGI(TAG, "Processing FOTA job: %s", job.job_id.c_str());
    ESP_LOGI(TAG, "Firmware URL: %s", job.firmware_url.c_str());
    ESP_LOGI(TAG, "Target version: %s", job.firmware_version.c_str());
    ESP_LOGI(TAG, "Firmware size: %u bytes", (unsigned int)job.firmware_size);
    
    // Update job status to IN_PROGRESS (non-fatal if rejected due to version mismatch)
    if (!UpdateJobStatus(job.job_id, JobExecutionStatus::IN_PROGRESS,
                         "Starting firmware download")) {
        ESP_LOGW(TAG, "Failed to update job status to IN_PROGRESS (continuing anyway)");
    }
    
    // Download and apply firmware with retry logic
    static constexpr int MAX_DOWNLOAD_ATTEMPTS = 3;
    static constexpr int RETRY_DELAY_MS = 5000;
    bool download_success = false;

    for (int attempt = 1; attempt <= MAX_DOWNLOAD_ATTEMPTS; attempt++) {
        ESP_LOGI(TAG, "Download attempt %d/%d", attempt, MAX_DOWNLOAD_ATTEMPTS);

        if (downloadFirmware(job.firmware_url, job.firmware_md5, job.firmware_size)) {
            download_success = true;
            break;
        }

        if (attempt < MAX_DOWNLOAD_ATTEMPTS) {
            char detail[128];
            snprintf(detail, sizeof(detail), "Download attempt %d/%d failed, retrying in %d seconds",
                     attempt, MAX_DOWNLOAD_ATTEMPTS, RETRY_DELAY_MS / 1000);
            ESP_LOGW(TAG, "%s", detail);
            UpdateJobStatus(job.job_id, JobExecutionStatus::IN_PROGRESS, detail);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        }
    }

    if (!download_success) {
        char detail[128];
        snprintf(detail, sizeof(detail), "Firmware download failed after %d attempts", MAX_DOWNLOAD_ATTEMPTS);
        UpdateJobStatus(job.job_id, JobExecutionStatus::FAILED, detail);
        return false;
    }
    
    // Update job status to SUCCEEDED
    UpdateJobStatus(job.job_id, JobExecutionStatus::SUCCEEDED, 
                   "Firmware update completed successfully");
    
    ESP_LOGI(TAG, "FOTA job completed successfully, rebooting...");
    
    // Reboot to apply new firmware
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return true;
}

bool AwsIotJobsHandler::downloadFirmware(const std::string& url, 
                                         const std::string& md5,
                                         uint32_t size) {
    ESP_LOGI(TAG, "Starting firmware download from: %s", url.c_str());
    
    esp_http_client_config_t config = {}; // Initialize all fields to zero/null
    config.url = url.c_str();
    config.timeout_ms = 30000;
    config.buffer_size = 4096;
    config.max_redirection_count = 5;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status: %d, Content-Length: %d", status_code, content_length);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP request failed with status %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Validate firmware fits in OTA partition (0x1B0000 = 1,769,472 bytes)
    static constexpr int OTA_PARTITION_SIZE = 0x1B0000;
    if (content_length > OTA_PARTITION_SIZE) {
        ESP_LOGE(TAG, "Firmware too large! %d bytes exceeds OTA partition size %d bytes",
                 content_length, OTA_PARTITION_SIZE);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Get OTA update partition
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "Failed to get OTA update partition");
        esp_http_client_cleanup(client);
        return false;
    }
    
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
         update_partition->subtype, (unsigned int)update_partition->address);
    
    esp_ota_handle_t update_handle = 0;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    
    // Download and write firmware
    char* buffer = (char*)malloc(4096);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        esp_ota_abort(update_handle);
        esp_http_client_cleanup(client);
        return false;
    }
    
    int binary_file_length = 0;
    bool success = true;
    mbedtls_md5_context md5_ctx;
    mbedtls_md5_init(&md5_ctx);
    mbedtls_md5_starts(&md5_ctx);

    // Overall download timeout: 10 minutes
    static constexpr int64_t DOWNLOAD_TIMEOUT_US = 10LL * 60 * 1000000;
    int64_t download_start = esp_timer_get_time();

    while (1) {
        // Check download timeout
        if ((esp_timer_get_time() - download_start) > DOWNLOAD_TIMEOUT_US) {
            ESP_LOGE(TAG, "Download timed out after 10 minutes");
            success = false;
            break;
        }

        int data_read = esp_http_client_read(client, buffer, 4096);
        if (data_read < 0) {
            ESP_LOGE(TAG, "Error reading data");
            success = false;
            break;
        } else if (data_read > 0) {
            // Update MD5 hash
            mbedtls_md5_update(&md5_ctx, (const unsigned char*)buffer, data_read);

            // Write to OTA partition
            err = esp_ota_write(update_handle, buffer, data_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error writing OTA data: %s", esp_err_to_name(err));
                success = false;
                break;
            }

            binary_file_length += data_read;

            // Report progress
            if (fota_progress_callback) {
                fota_progress_callback(binary_file_length, size);
            }

            ESP_LOGD(TAG, "Downloaded %d/%u bytes", binary_file_length, (unsigned int)size);
        } else {
            // Download complete
            break;
        }
    }
    
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    
    // Post-download size validation using HTTP Content-Length (not job doc size)
    if (success && content_length > 0 && binary_file_length != content_length) {
        ESP_LOGE(TAG, "Size mismatch! Downloaded %d bytes but Content-Length was %d (truncated download?)",
                 binary_file_length, content_length);
        success = false;
    }

    if (success) {
        // Verify MD5 (skip if no MD5 was provided in job document)
        unsigned char md5_output[16];
        mbedtls_md5_finish(&md5_ctx, md5_output);

        char md5_str[33];
        for (int i = 0; i < 16; i++) {
            sprintf(&md5_str[i * 2], "%02x", md5_output[i]);
        }
        md5_str[32] = '\0';

        if (md5.empty()) {
            ESP_LOGW(TAG, "No MD5 provided in job document, skipping verification (computed: %s)", md5_str);
        } else if (md5.compare(md5_str) != 0) {
            ESP_LOGE(TAG, "MD5 verification failed. Expected: %s, Got: %s",
                     md5.c_str(), md5_str);
            success = false;
        } else {
            ESP_LOGI(TAG, "MD5 verification successful");
        }
    }
    
    mbedtls_md5_free(&md5_ctx);
    
    if (success) {
        err = esp_ota_end(update_handle);
        if (err != ESP_OK) {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Firmware image is corrupt — OTA validation failed!");
            } else {
                ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            }
            return false;
        }
        
        // Set new firmware as boot partition
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
            return false;
        }
        
        ESP_LOGI(TAG, "Firmware download successful. Total size: %d bytes", binary_file_length);
    } else {
        esp_ota_abort(update_handle);
    }
    
    return success;
}