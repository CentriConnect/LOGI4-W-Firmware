#include "UdpTelemetryClient.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "mbedtls/md.h"

#include <cstdio>
#include <cstring>
#include <sys/types.h>
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
    const char* dts = context.dateTimeIsoValid ? context.dateTimeIso : "";
    const char* err = context.errorLogValid ? context.errorLog : "";
    const char* sch = context.mqttSchemaValid ? context.mqttSchema : "";
    const char* ver = context.deviceVersionValid ? context.deviceVersion : "";

    char buf[768];
    snprintf(buf, sizeof(buf),
             "%s|%s|%d|%s|%s|%s|%u|%ld|%s|%d|%s|%s|%s|%s",
             formatCompactFloat(data.MeasuredTemperatureC).c_str(),
             formatFixed2(data.AnalogBatteryVoltage).c_str(),
             static_cast<int>(data.PublishedBatteryLevel),
             dev,
             dts,
             err,
             static_cast<unsigned>(data.PublishedFuelLevel),
             static_cast<long>(context.lteSignalQualityValid ? context.lteSignalQuality : 0),
             formatFixed2(data.AnalogFuelVoltage).c_str(),
             static_cast<int>(context.resetCounterValid ? context.resetCounter : 0),
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
    const char* dts = context.dateTimeIsoValid ? context.dateTimeIso : "";
    const char* sch = context.mqttSchemaValid ? context.mqttSchema : "";
    const char* ver = context.deviceVersionValid ? context.deviceVersion : "";
    const char* err = context.errorLogValid ? context.errorLog : "";

    char payload[UDP_PAYLOAD_MAX_BYTES + 1];
    int written = snprintf(payload, sizeof(payload),
                           "{\"dev\":\"%s\",\"dts\":\"%s\",\"sch\":\"%s\",\"ver\":\"%s\","
                           "\"lsq\":%ld,\"bat\":%s,\"ful\":%u,\"amb\":%s,\"sol\":%s,"
                           "\"chg\":%u,\"rst\":%d,\"err\":\"%s\",\"raw\":%s,\"supv\":%s,"
                           "\"sig\":\"%s\"}",
                           jsonEscape(dev).c_str(),
                           jsonEscape(dts).c_str(),
                           jsonEscape(sch).c_str(),
                           jsonEscape(ver).c_str(),
                           static_cast<long>(context.lteSignalQualityValid ? context.lteSignalQuality : 0),
                           formatFixed2(data.AnalogBatteryVoltage).c_str(),
                           static_cast<unsigned>(data.PublishedFuelLevel),
                           formatCompactFloat(data.MeasuredTemperatureC).c_str(),
                           formatFixed2(data.SolarVoltage).c_str(),
                           static_cast<unsigned>(data.PublishedBatteryLevel),
                           static_cast<int>(context.resetCounterValid ? context.resetCounter : 0),
                           jsonEscape(err).c_str(),
                           formatFixed2(data.AnalogFuelVoltage).c_str(),
                           formatFixed2(data.SensorSupplyVoltage).c_str(),
                           sig.c_str());

    if (written < 0 || static_cast<size_t>(written) >= UDP_PAYLOAD_MAX_BYTES)
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

    ESP_LOGI(TAG, "Sending UDP telemetry (%u bytes)", static_cast<unsigned>(payload.size()));

    char port[8];
    snprintf(port, sizeof(port), "%d", CONFIG_LOGI_UDP_POST_PORT);

    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo* res = nullptr;
    int err = getaddrinfo(CONFIG_LOGI_UDP_POST_HOST, port, &hints, &res);
    if (err != 0 || res == nullptr)
    {
        ESP_LOGE(TAG, "UDP host lookup failed for %s:%s (%d)", CONFIG_LOGI_UDP_POST_HOST, port, err);
        return false;
    }

    bool sent = false;
    for (struct addrinfo* ai = res; ai != nullptr && !sent; ai = ai->ai_next)
    {
        int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0)
        {
            continue;
        }

        ssize_t sentBytes = sendto(sock,
                                   payload.data(),
                                   payload.size(),
                                   0,
                                   ai->ai_addr,
                                   ai->ai_addrlen);
        if (sentBytes == static_cast<ssize_t>(payload.size()))
        {
            sent = true;
        }
        else
        {
            ESP_LOGW(TAG, "UDP send failed: sent %d of %u bytes",
                     static_cast<int>(sentBytes),
                     static_cast<unsigned>(payload.size()));
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
