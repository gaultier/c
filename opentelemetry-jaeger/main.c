#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define CJSON_HIDE_SYMBOLS
#include "vendor/cJSON/cJSON.h"

int main() {
    cJSON* root = cJSON_CreateObject();

    cJSON* resourceSpans = cJSON_AddArrayToObject(root, "resourceSpans");

    cJSON* resourceSpan = cJSON_CreateObject();
    cJSON_AddItemToArray(resourceSpans, resourceSpan);

    cJSON* resource = cJSON_AddObjectToObject(resourceSpan, "resource");

    cJSON* attributes = cJSON_AddArrayToObject(resource, "attributes");
    cJSON* attribute = cJSON_CreateObject();
    cJSON_AddItemToArray(attributes, attribute);
    cJSON_AddStringToObject(attribute, "key", "service.name");
    cJSON* value = cJSON_CreateObject();
    cJSON_AddItemToObject(attribute, "value", value);
    cJSON_AddStringToObject(value, "stringValue", "main.c");

    cJSON* instrumentationLibrarySpans =
        cJSON_AddArrayToObject(resourceSpan, "instrumentationLibrarySpans");
    cJSON* instrumentationLibrarySpan = cJSON_CreateObject();
    cJSON_AddItemToArray(instrumentationLibrarySpans,
                         instrumentationLibrarySpan);

    cJSON* spans = cJSON_AddArrayToObject(instrumentationLibrarySpan, "spans");

    struct timeval start = {0};
    gettimeofday(&start, NULL);


    uint8_t traceId[16] = {};
    arc4random_buf(traceId, sizeof(traceId));

    uint8_t spanIdA[8] = {};
    arc4random_buf(spanIdA, sizeof(spanIdA));
    {
    cJSON* spanA = cJSON_CreateObject();
    cJSON_AddItemToArray(spans, spanA);
    cJSON* start_ns = cJSON_CreateNumber(
        (start.tv_sec - 3) * 1000 * 1000 * 1000 + start.tv_usec * 1000);
    cJSON_AddItemToObject(spanA, "startTimeUnixNano", start_ns);
    cJSON_AddNumberToObject(
        spanA, "endTimeUnixNano",
        (start.tv_sec - 1) * 1000 * 1000 * 1000 + start.tv_usec * 1000 );

    char buf[64] = "";
    for (int i = 0; i < sizeof(traceId); i++) {
        snprintf(&buf[i * 2], sizeof(buf), "%02x", traceId[i]);
    }
    cJSON_AddStringToObject(spanA, "traceId", buf);

    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < sizeof(spanIdA); i++) {
        snprintf(&buf[i * 2], sizeof(buf), "%02x", spanIdA[i]);
    }
    cJSON_AddStringToObject(spanA, "spanId", buf);
    cJSON_AddNumberToObject(spanA, "kind", 1);
    cJSON* status = cJSON_AddObjectToObject(spanA, "status");
    cJSON_AddNumberToObject(status, "code", 9);

    cJSON_AddStringToObject(spanA, "name", "A");
    }
    {
    cJSON* spanB = cJSON_CreateObject();
    cJSON_AddItemToArray(spans, spanB);
    cJSON* start_ns = cJSON_CreateNumber(
        (start.tv_sec - 2) * 1000 * 1000 * 1000 + start.tv_usec * 1000 );
    cJSON_AddItemToObject(spanB, "startTimeUnixNano", start_ns);
    cJSON_AddNumberToObject(
        spanB, "endTimeUnixNano",
        (start.tv_sec - 1) * 1000 * 1000 * 1000 + start.tv_usec * 1000 + 1000);

    uint8_t spanIdB[8] = {};
    arc4random_buf(spanIdB, sizeof(spanIdB));

    char buf[64] = "";
    for (int i = 0; i < sizeof(traceId); i++) {
        snprintf(&buf[i * 2], sizeof(buf), "%02x", traceId[i]);
    }
    cJSON_AddStringToObject(spanB, "traceId", buf);

    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < sizeof(spanIdB); i++) {
        snprintf(&buf[i * 2], sizeof(buf), "%02x", spanIdB[i]);
    }
    cJSON_AddStringToObject(spanB, "spanId", buf);

    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < sizeof(spanIdA); i++) {
        snprintf(&buf[i * 2], sizeof(buf), "%02x", spanIdA[i]);
    }
    cJSON_AddStringToObject(spanB, "parentSpanId", buf);

    cJSON_AddNumberToObject(spanB, "kind", 1);
    cJSON* status = cJSON_AddObjectToObject(spanB, "status");
    cJSON_AddNumberToObject(status, "code", 8);

    cJSON_AddStringToObject(spanB, "name", "B");
    }

    puts(cJSON_Print(root));
}
