#ifndef RENTAL_PROTOCOL_H
#define RENTAL_PROTOCOL_H
#include <stddef.h>
#include <string.h>
#define RENTAL_LINE_MAX 256U
#define RENTAL_ID_MAX 31U
#define RENTAL_ALARM_LOCKERS 16U

static inline int rental_detection_status(const char *status)
{
    return !strcmp(status,"SAVED") || !strcmp(status,"RENTED") || !strcmp(status,"RETURNED") ||
           !strcmp(status,"BUSY") || !strcmp(status,"UNCHANGED") ||
           !strcmp(status,"ERROR") || !strcmp(status,"REJECTED");
}

static inline int rental_alarm_status(const char *body)
{
    if (strcmp(body, "ALARM@ACTIVE") == 0) return 1;
    if (strcmp(body, "ALARM@CLEAR") == 0) return 0;
    return -1;
}

static inline int rental_valid_id(const char *id)
{
    size_t n = strlen(id);
    if (n == 0 || n > RENTAL_ID_MAX) return 0;
    for (size_t i = 0; i < n; ++i) {
        char c = id[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return 1;
}
static inline int rental_parse_frame(const char *line, char id[32], const char **body)
{
    if (line[0] != '[') return 0;
    const char *end = strchr(line, ']');
    if (!end || end == line + 1 || (size_t)(end - line - 1) > RENTAL_ID_MAX) return 0;
    size_t n = (size_t)(end - line - 1);
    memcpy(id, line + 1, n);
    id[n] = '\0';
    *body = end + 1;
    return rental_valid_id(id);
}
static inline int rental_valid_uid(const char *uid)
{
    size_t n = strlen(uid);
    return (n == 8U || n == 14U || n == 20U) && strspn(uid, "0123456789ABCDEF") == n;
}
static inline int rental_valid_request(const char *request)
{
    return strlen(request) == 16U && strspn(request, "0123456789ABCDEF") == 16U;
}
static inline int rental_parse_card(const char *value, char uid[21], char request[17])
{
    const char *separator = strchr(value, '@');
    if (!separator || separator == value || (size_t)(separator - value) > 20U) return 0;
    size_t n = (size_t)(separator - value);
    memcpy(uid, value, n);
    uid[n] = '\0';
    if (!rental_valid_uid(uid) || !rental_valid_request(separator + 1)) return 0;
    memcpy(request, separator + 1, 17);
    return 1;
}
static inline int rental_parse_auth(const char *value, char uid[21], char request[17], char status[9])
{
    const char *last = strrchr(value, '@');
    if (!last || (size_t)(last - value) >= 40U) return 0;
    char card[40];
    size_t n = (size_t)(last - value);
    memcpy(card, value, n);
    card[n] = '\0';
    if (!rental_parse_card(card, uid, request)) return 0;
    const char *result = last + 1;
    if (strcmp(result, "APPROVED") && strcmp(result, "DENIED") && strcmp(result, "ERROR")) return 0;
    strcpy(status, result);
    return 1;
}

static inline int rental_parse_sensor(const char *value, unsigned int *adc, char request[17])
{
    const char *separator = strchr(value, '@');
    if (!separator || separator == value || separator - value > 4 ||
        !rental_valid_request(separator + 1)) return 0;
    unsigned int number = 0;
    for (const char *p = value; p < separator; ++p) {
        if (*p < '0' || *p > '9') return 0;
        number = number * 10U + (unsigned int)(*p - '0');
    }
    if (number > 4095U) return 0;
    *adc = number;
    memcpy(request, separator + 1, 17);
    return 1;
}

static inline int rental_valid_room(const char *room)
{
    size_t n = strlen(room);
    if (!n || n > 10U) return 0;
    for (size_t i = 0; i < n; ++i) {
        char c = room[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '-' || c == '_')) return 0;
    }
    return 1;
}

static inline int rental_parse_user(const char *value, char uid[21], char request[17], char room[11])
{
    const char *last = strrchr(value, '@');
    if (!last || (size_t)(last - value) >= 40U || !rental_valid_room(last + 1)) return 0;
    char card[40];
    size_t n = (size_t)(last - value);
    memcpy(card, value, n);
    card[n] = '\0';
    if (!rental_parse_card(card, uid, request)) return 0;
    strcpy(room, last + 1);
    return 1;
}

static inline int rental_parse_sensor_reply(const char *value, char request[17], char status[6])
{
    const char *separator = strchr(value, '@');
    if (!separator || separator - value != 16) return 0;
    memcpy(request, value, 16);
    request[16] = '\0';
    if (!rental_valid_request(request) ||
        (strcmp(separator + 1, "SAVED") && strcmp(separator + 1, "ERROR"))) return 0;
    strcpy(status, separator + 1);
    return 1;
}

static inline int rental_field(const char **cursor, char *out, size_t capacity, int last)
{
    const char *end = strchr(*cursor, '@');
    if ((last && end) || (!last && !end)) return 0;
    size_t n = end ? (size_t)(end - *cursor) : strlen(*cursor);
    if (!n || n >= capacity) return 0;
    memcpy(out, *cursor, n);
    out[n] = '\0';
    *cursor = end ? end + 1 : *cursor + n;
    return 1;
}
static inline int rental_parse_session(const char *value, char uid[21], char session[17], char status[7])
{
    return rental_field(&value, uid, 21, 0) && rental_field(&value, session, 17, 0) &&
           rental_field(&value, status, 7, 1) && rental_valid_uid(uid) && rental_valid_request(session) &&
           (!strcmp(status, "OPEN") || !strcmp(status, "CLOSED"));
}
static inline int rental_confidence(const char *value, unsigned int *confidence)
{
    size_t n = strlen(value);
    if (!n || n > 4) return 0;
    unsigned int number = 0;
    for (size_t i = 0; i < n; ++i) {
        if (value[i] < '0' || value[i] > '9') return 0;
        number = number * 10U + (unsigned)(value[i] - '0');
    }
    if (number < 900U || number > 1000U) return 0;
    *confidence = number;
    return 1;
}
static inline int rental_parse_detect(const char *value, char session[17], char label[32],
                                      unsigned int *confidence, char event[17])
{
    char number[5];
    return rental_field(&value, session, 17, 0) && rental_field(&value, label, 32, 0) &&
           rental_field(&value, number, 5, 0) && rental_field(&value, event, 17, 1) &&
           rental_valid_request(session) && rental_valid_id(label) && strcmp(label, "NULL") &&
           rental_confidence(number, confidence) && rental_valid_request(event);
}
static inline int rental_parse_db_detect(const char *value, char uid[21], char session[17],
                                         char camera[32], char label[32], unsigned int *confidence, char event[17])
{
    char number[5];
    return rental_field(&value, uid, 21, 0) && rental_valid_uid(uid) &&
           rental_field(&value, session, 17, 0) && rental_valid_request(session) &&
           rental_field(&value, camera, 32, 0) && rental_valid_id(camera) &&
           rental_field(&value, label, 32, 0) && rental_valid_id(label) && strcmp(label, "NULL") &&
           rental_field(&value, number, 5, 0) && rental_confidence(number, confidence) &&
           rental_field(&value, event, 17, 1) && rental_valid_request(event);
}
#endif
