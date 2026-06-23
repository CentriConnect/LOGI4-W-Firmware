#ifndef STATES_COMMON_H
#define STATES_COMMON_H

enum class ApplicationState
{
    ApplicationState_Wake,
    ApplicationState_Provisioning,  // BLE WiFi provisioning mode
    ApplicationState_DataSample,
    ApplicationState_ScheduleCheck,
    ApplicationState_CheckFillDetect,
    ApplicationState_Posting,
    ApplicationState_Sleep
};

enum class WakeState 
{
    WakeState_CheckStartupCause,
};

enum class DataSampleState 
{
    DataSampleState_SampleSensorData,
    DataSampleState_UpdateLocalValues
};

enum class ScheduleCheckState 
{
    ScheduleCheckState_ValidateDateTime,
    ScheduleCheckState_CheckProvisioning,
    ScheduleCheckState_CompareTimeToScheduledTime
};

enum class CheckFillDetectState 
{
    CheckFillDetectState_CheckInDwellMode,
    CheckFillDetectState_CompareFuelLevels,
    CheckFillDetectState_CompareFuelLevelsWithDelta,
    CheckFillDetectState_CheckDwellTime
};

enum class PostingState
{
    PostingState_InitialEnter,
    PostingState_ExitAWS,
    PostingState_TryConnect,
    PostingState_PostImmediateTelemetry,
    PostingState_SubscribeToTopics,
    PostingState_SendGetShadowDelta,
    PostingState_GotShadowDelta,
    PostingState_HandleShadowDelta,
    PostingState_CheckFOTA,
    PostingState_DoFOTAUpdate,
    PostingState_AcquireFinalSample,
    PostingState_PostFinalTelemetry,
    PostingState_DoPostsFromQueue,
    PostingState_PostDwell          // Wait after successful post for cloud commands/OTA
};

enum class PostTransport
{
    PostTransport_Mqtt,
    PostTransport_Udp
};

enum class ProvisioningState
{
    ProvisioningState_Init,             // Initialize BLE provisioning service
    ProvisioningState_Advertising,      // BLE advertising, waiting for connection
    ProvisioningState_Connected,        // Mobile app connected, awaiting credentials
    ProvisioningState_VerifyCredentials,// Testing WiFi credentials
    ProvisioningState_Success,          // Credentials verified, save to NVS
    ProvisioningState_Failed,           // Credential verification failed
    ProvisioningState_PostJobsShadow,   // REQ-FIRSTBOOT-01: post -> jobs -> shadow -> post
    ProvisioningState_SuccessDisplay,   // Green LED blink before reboot
    ProvisioningState_Timeout           // Advertising timeout reached
};

// Provisioning mode type
enum class ProvisioningMode
{
    FirstBoot,      // No credentials exist - 15 min advertise, 15 min sleep
    ReProvision     // Credentials wiped due to failures - 3 min advertise, 15 min sleep
};

// WiFi connection failure types for provisioning decisions
enum class WifiFailureType
{
    WifiFailure_None,           // No failure
    WifiFailure_AuthError,      // Wrong password (triggers re-provision after 10x)
    WifiFailure_ApNotFound,     // SSID not found (triggers re-provision immediately)
    WifiFailure_Timeout,        // Connection timeout (do NOT trigger re-provision)
    WifiFailure_NvsCorruption   // Cannot read credentials (triggers first-boot provision)
};

enum class LedState
{
    LedState_Off,
    LedState_GreenBlink,
    LedState_YellowBlink,
    LedState_BlueBlink,
    LedState_RedBlink,
    LedState_RedSolid,
    LedState_RedFastBlink
};

#endif // STATES_COMMON_H
