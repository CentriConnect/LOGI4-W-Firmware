#include "UdpTelemetryClient.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "mbedtls/md.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef CONFIG_LOGI_UDP_POST_ENABLED
#define CONFIG_LOGI_UDP_POST_ENABLED 0
#endif

#ifndef CONFIG_LOGI_UDP_POST_HOST
#define CONFIG_LOGI_UDP_POST_HOST ""
#endif

#ifndef CONFIG_LOGI_UDP_POST_PORT
#define CONFIG_LOGI_UDP_POST_PORT 0
#endif

#ifndef CONFIG_LOGI_UDP_POST_HMAC_SECRET
#define CONFIG_LOGI_UDP_POST_HMAC_SECRET ""
#endif

namespace
{
constexpr size_t UDP_PAYLOAD_MAX_BYTES = 512;
constexpr char TAG[] = "UdpTelemetry";
constexpr int DNS_LOOKUP_MAX_ATTEMPTS = 3;
constexpr uint32_t DNS_LOOKUP_RETRY_DELAY_MS = 1000;
constexpr int UDP_ACK_TIMEOUT_MS = 2000;
constexpr float UDP_BATTERY_TEMP_UNAVAILABLE_C = -1.0f;

std::string jsonEscape(const char* value)
{
    std::string escaped;
    if (value == nullptr)
    {
        return escaped;
    }

    for (const char* p = value; *p != '\0'; ++p)
    {
        switch (*p)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(*p) < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(*p));
                escaped += buf;
            }
            else
            {
                escaped += *p;
            }
            break;
        }
    }

    return escaped;
}

std::string formatFixed2(float value)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));
    return std::string(buf);
}

std::string formatCompactFloat(float value)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));

    char* dot = strchr(buf, '.');
    if (dot != nullptr)
    {
        char* end = buf + strlen(buf) - 1;
        while (end > dot && *end == '0')
        {
            *end-- = '\0';
        }
        if (end == dot)
        {
            *end = '\0';
        }
    }

    return std::string(buf);
}

std::string formatUdpTimestamp(const char* value)
{
    if (value == nullptr || value[0] == '\0')
    {
        return std::string();
    }

    std::string timestamp(value);
    constexpr const char* millisSuffix = ".000";
    if (timestamp.size() >= strlen(millisSuffix) &&
        timestamp.compare(timestamp.size() - strlen(millisSuffix), strlen(millisSuffix), millisSuffix) == 0)
    {
        timestamp.resize(timestamp.size() - strlen(millisSuffix));
    }

    if (timestamp.back() != 'Z')
    {
        timestamp += 'Z';
    }
    return timestamp;
}

std::string hmacSha256Hex(const char* key, const std::string& message)
{
    unsigned char digest[32] = {0};
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mdInfo == nullptr)
    {
        ESP_LOGE(TAG, "Unable to load SHA256 HMAC implementation");
        return std::string();
    }

    int rc = mbedtls_md_hmac(mdInfo,
                             reinterpret_cast<const unsigned char*>(key),
                             strlen(key),
                             reinterpret_cast<const unsigned char*>(message.data()),
                             message.size(),
                             digest);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "HMAC-SHA256 failed: %d", rc);
        return std::string();
    }

    char hex[65];
    for (size_t i = 0; i < sizeof(digest); ++i)
    {
        snprintf(&hex[i * 2], 3, "%02x", digest[i]);
    }
    hex[64] = '\0';
    return std::string(hex);
}

const char* gaiErrorName(int err)
{
    switch (err)
    {
    case EAI_NONAME:
        return "EAI_NONAME";
    case EAI_SERVICE:
        return "EAI_SERVICE";
    case EAI_FAIL:
        return "EAI_FAIL";
    case EAI_MEMORY:
        return "EAI_MEMORY";
    case EAI_FAMILY:
        return "EAI_FAMILY";
    default:
        return "UNKNOWN";
    }
}
} // namespace

bool UdpTelemetryClient::IsEnabled()
{
#if CONFIG_LOGI_UDP_POST_ENABLED
    return true;
#else
    return false;
#endif
}

std::string UdpTelemetryClient::CreateCanonicalString(const LogiSensorData& data, const TelemetryContext& context)
{
    const char* dev = context.deviceIdValid ? context.deviceId : "";
    const std::string dts = formatUdpTimestamp(context.dateTimeIsoValid ? context.dateTimeIso : "");
    const char* err = context.errorLogValid ? context.errorLog : "";
    const char* sch = context.mqttSchemaValid ? context.mqttSchema : "";
    const char* ver = context.deviceVersionValid ? context.deviceVersion : "";
    const int chargerStatus = context.chargerStatusValid ? static_cast<int>(context.chargerStatus) : 0;
    const int deviceStatus = context.resetCounterValid ? static_cast<int>(context.resetCounter) : 0;

    char buf[768];
    snprintf(buf, sizeof(buf),
             "%s|%s|%s|%d|%s|%s|%s|%u|%ld|%s|%d|%s|%s|%s|%s",
             formatCompactFloat(data.MeasuredTemperatureC).c_str(),
             formatFixed2(data.AnalogBatteryVoltage).c_str(),
             formatFixed2(UDP_BATTERY_TEMP_UNAVAILABLE_C).c_str(),
             chargerStatus,
             dev,
             dts.c_str(),
             err,
             static_cast<unsigned>(data.PublishedFuelLevel),
             static_cast<long>(context.lteSignalQualityValid ? context.lteSignalQuality : 0),
             formatFixed2(data.AnalogFuelVoltage).c_str(),
             deviceStatus,
             sch,
             formatFixed2(data.SolarVoltage).c_str(),
             formatFixed2(data.SensorSupplyVoltage).c_str(),
             ver);

    return std::string(buf);
}

std::string UdpTelemetryClient::CreatePayload(const LogiSensorData& data, const TelemetryContext& context)
{
    const std::string canonical = CreateCanonicalString(data, context);
    const std::string sig = hmacSha256Hex(CONFIG_LOGI_UDP_POST_HMAC_SECRET, canonical);
    if (sig.empty())
    {
        return std::string();
    }

    const char* dev = context.deviceIdValid ? context.deviceId : "";
    const std::string dts = formatUdpTimestamp(context.dateTimeIsoValid ? context.dateTimeIso : "");
    const char* sch = context.mqttSchemaValid ? context.mqttSchema : "";
    const char* ver = context.deviceVersionValid ? context.deviceVersion : "";
    const char* err = context.errorLogValid ? context.errorLog : "";
    const int chargerStatus = context.chargerStatusValid ? static_cast<int>(context.chargerStatus) : 0;
    const int deviceStatus = context.resetCounterValid ? static_cast<int>(context.resetCounter) : 0;

    char payload[UDP_PAYLOAD_MAX_BYTES + 1];
    int written = snprintf(payload, sizeof(payload),
                           "{\"dev\":\"%s\",\"dts\":\"%s\",\"sch\":\"%s\",\"ver\":\"%s\","
                           "\"lsq\":%ld,\"bat\":%s,\"ful\":%u,\"amb\":%s,\"sol\":%s,"
                           "\"chg\":%d,\"rst\":%d,\"err\":\"%s\",\"raw\":%s,\"supv\":%s,\"btmp\":%s,"
                           "\"sig\":\"%s\"}",
                           jsonEscape(dev).c_str(),
                           jsonEscape(dts.c_str()).c_str(),
                           jsonEscape(sch).c_str(),
                           jsonEscape(ver).c_str(),
                           static_cast<long>(context.lteSignalQualityValid ? context.lteSignalQuality : 0),
                           formatFixed2(data.AnalogBatteryVoltage).c_str(),
                           static_cast<unsigned>(data.PublishedFuelLevel),
                           formatCompactFloat(data.MeasuredTemperatureC).c_str(),
                           formatFixed2(data.SolarVoltage).c_str(),
                           chargerStatus,
                           deviceStatus,
                           jsonEscape(err).c_str(),
                           formatFixed2(data.AnalogFuelVoltage).c_str(),
                           formatFixed2(data.SensorSupplyVoltage).c_str(),
                           formatFixed2(UDP_BATTERY_TEMP_UNAVAILABLE_C).c_str(),
                           sig.c_str());

    if (written < 0 || static_cast<size_t>(written) > UDP_PAYLOAD_MAX_BYTES)
    {
        ESP_LOGE(TAG, "UDP telemetry payload too large: %d bytes", written);
        return std::string();
    }

    return std::string(payload, static_cast<size_t>(written));
}

bool UdpTelemetryClient::SendTelemetry(const LogiSensorData& data, const TelemetryContext& context)
{
    if (!IsEnabled())
    {
        ESP_LOGW(TAG, "UDP telemetry requested but LOGI_UDP_POST_ENABLED is disabled");
        return false;
    }

    if (strlen(CONFIG_LOGI_UDP_POST_HOST) == 0 || CONFIG_LOGI_UDP_POST_PORT <= 0)
    {
        ESP_LOGE(TAG, "UDP posting enabled but host/port is not configured");
        return false;
    }
    if (strlen(CONFIG_LOGI_UDP_POST_HMAC_SECRET) == 0)
    {
        ESP_LOGE(TAG, "UDP posting enabled but HMAC secret is empty");
        return false;
    }

    const std::string payload = CreatePayload(data, context);
    if (payload.empty())
    {
        return false;
    }
    ESP_LOGI(TAG, "UDP HMAC canonical: %s", CreateCanonicalString(data, context).c_str());
    ESP_LOGI(TAG, "UDP telemetry payload: %s", payload.c_str());

    ESP_LOGI(TAG, "Preparing UDP telemetry (%u bytes) for %s:%d",
             static_cast<unsigned>(payload.size()),
             CONFIG_LOGI_UDP_POST_HOST,
             CONFIG_LOGI_UDP_POST_PORT);

    char port[8];
    snprintf(port, sizeof(port), "%d", CONFIG_LOGI_UDP_POST_PORT);

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo* res = nullptr;
    int err = EAI_FAIL;
    for (int attempt = 1; attempt <= DNS_LOOKUP_MAX_ATTEMPTS; ++attempt)
    {
        err = getaddrinfo(CONFIG_LOGI_UDP_POST_HOST, port, &hints, &res);
        if (err == 0 && res != nullptr)
        {
            break;
        }

        ESP_LOGW(TAG, "UDP host lookup attempt %d/%d failed for %s:%s (%d/%s)",
                 attempt,
                 DNS_LOOKUP_MAX_ATTEMPTS,
                 CONFIG_LOGI_UDP_POST_HOST,
                 port,
                 err,
                 gaiErrorName(err));

        if (attempt < DNS_LOOKUP_MAX_ATTEMPTS)
        {
            vTaskDelay(pdMS_TO_TICKS(DNS_LOOKUP_RETRY_DELAY_MS));
        }
    }

    if (err != 0 || res == nullptr)
    {
        ESP_LOGE(TAG, "UDP host lookup failed for %s:%s after %d attempts (%d/%s)",
                 CONFIG_LOGI_UDP_POST_HOST,
                 port,
                 DNS_LOOKUP_MAX_ATTEMPTS,
                 err,
                 gaiErrorName(err));
        return false;
    }

    bool sent = false;
    for (struct addrinfo* ai = res; ai != nullptr && !sent; ai = ai->ai_next)
    {
        char addrText[INET_ADDRSTRLEN] = {0};
        void* addrPtr = nullptr;
        if (ai->ai_family == AF_INET)
        {
            addrPtr = &reinterpret_cast<struct sockaddr_in*>(ai->ai_addr)->sin_addr;
        }
        if (addrPtr != nullptr)
        {
            inet_ntop(ai->ai_family, addrPtr, addrText, sizeof(addrText));
        }

        int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0)
        {
            ESP_LOGW(TAG, "UDP socket creation failed for resolved address %s: errno=%d",
                     addrText[0] ? addrText : "<unknown>",
                     errno);
            continue;
        }

        ESP_LOGI(TAG, "Sending UDP telemetry to %s:%d (%u bytes)",
                 addrText[0] ? addrText : CONFIG_LOGI_UDP_POST_HOST,
                 CONFIG_LOGI_UDP_POST_PORT,
                 static_cast<unsigned>(payload.size()));

        ssize_t sentBytes = sendto(sock,
                                   payload.data(),
                                   payload.size(),
                                   0,
                                   ai->ai_addr,
                                   ai->ai_addrlen);
        if (sentBytes == static_cast<ssize_t>(payload.size()))
        {
            sent = true;
            timeval ackTimeout = {};
            ackTimeout.tv_sec = UDP_ACK_TIMEOUT_MS / 1000;
            ackTimeout.tv_usec = (UDP_ACK_TIMEOUT_MS % 1000) * 1000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &ackTimeout, sizeof(ackTimeout));

            char ack[8] = {0};
            sockaddr_storage ackSource = {};
            socklen_t ackSourceLen = sizeof(ackSource);
            ssize_t ackBytes = recvfrom(sock,
                                        ack,
                                        sizeof(ack) - 1,
                                        0,
                                        reinterpret_cast<sockaddr*>(&ackSource),
                                        &ackSourceLen);
            if (ackBytes > 0)
            {
                ack[ackBytes] = '\0';
                ESP_LOGI(TAG, "UDP server ACK received: %s", ack);
            }
            else
            {
                ESP_LOGW(TAG, "No UDP server ACK received within %d ms; server may have dropped validation/SQS enqueue",
                         UDP_ACK_TIMEOUT_MS);
            }
        }
        else
        {
            ESP_LOGW(TAG, "UDP send failed to %s: sent %d of %u bytes, errno=%d",
                     addrText[0] ? addrText : "<unknown>",
                     static_cast<int>(sentBytes),
                     static_cast<unsigned>(payload.size()),
                     errno);
        }
        close(sock);
    }

    freeaddrinfo(res);

    if (!sent)
    {
        ESP_LOGE(TAG, "Failed to send UDP telemetry");
    }
    return sent;
}
