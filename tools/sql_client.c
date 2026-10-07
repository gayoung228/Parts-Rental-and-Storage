/* C DB worker: receives STM32 messages through the relay, writes DB, replies AUTH. */
#define _POSIX_C_SOURCE 200809L
#include "rental_protocol.h"
#include <arpa/inet.h>
#include <errno.h>
#include <mysql.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
static char last_approved_room[11];
#include "rental_db.h"

static const char *db_env(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value && *value ? value : fallback;
}

static MYSQL *db_connect(void)
{
    const char *password = getenv("DB_PASSWORD");
    if (!password || !*password) return NULL;
    MYSQL *db = mysql_init(NULL);
    if (!db) return NULL;
    unsigned timeout = 2;
    if (mysql_options(db, MYSQL_OPT_CONNECT_TIMEOUT, &timeout) ||
        mysql_options(db, MYSQL_OPT_READ_TIMEOUT, &timeout) ||
        mysql_options(db, MYSQL_OPT_WRITE_TIMEOUT, &timeout) ||
        !mysql_real_connect(db, db_env("DB_HOST", "localhost"), db_env("DB_USER", "parts_app"),
                            password, db_env("DB_NAME", "parts_rental"), 0, getenv("DB_SOCKET"), 0)) {
        fprintf(stderr, "DB connection failed (code %u)\n", mysql_errno(db));
        mysql_close(db);
        return NULL;
    }
    return db;
}

/* 1: enabled registered card, 0: disabled/unknown, -1: DB error. */
static int db_lookup_card(MYSQL *db, const char *uid, int *household_id, char room[11])
{
    MYSQL_STMT *stmt = mysql_stmt_init(db);
    if (!stmt) return -1;
    const char query[] = "SELECT c.enabled, c.household_id, h.unit_no FROM rfid_cards c "
                         "JOIN households h ON h.id=c.household_id "
                         "WHERE c.uid=? LIMIT 1 LOCK IN SHARE MODE";
    MYSQL_BIND input[1] = {{0}}, output[3] = {{0}};
    unsigned long uid_length = (unsigned long)strlen(uid);
    int enabled = 0, house = 0, result = -1;
    my_bool is_null[3] = {0, 0, 0};
    unsigned long room_length = 0;
    room[0] = '\0';
    input[0].buffer_type = MYSQL_TYPE_STRING;
    input[0].buffer = (void *)uid;
    input[0].buffer_length = uid_length;
    input[0].length = &uid_length;
    output[0].buffer_type = MYSQL_TYPE_LONG;
    output[0].buffer = &enabled;
    output[0].is_null = &is_null[0];
    output[1].buffer_type = MYSQL_TYPE_LONG;
    output[1].buffer = &house;
    output[1].is_null = &is_null[1];
    output[2].buffer_type = MYSQL_TYPE_STRING;
    output[2].buffer = room;
    output[2].buffer_length = 10;
    output[2].length = &room_length;
    output[2].is_null = &is_null[2];
    if (mysql_stmt_prepare(stmt, query, sizeof(query) - 1U) ||
        mysql_stmt_bind_param(stmt, input) || mysql_stmt_bind_result(stmt, output) ||
        mysql_stmt_execute(stmt)) goto done;
    int fetched = mysql_stmt_fetch(stmt);
    if (fetched == MYSQL_NO_DATA) result = 0;
    else if (fetched == 0 && !is_null[0] && !is_null[1] && !is_null[2] && house > 0 && room_length <= 10U) {
        room[room_length] = '\0';
        if (!rental_valid_room(room)) goto done;
        *household_id = house;
        result = enabled == 1 ? 1 : 0;
    }
done:
    if (result < 0) fprintf(stderr, "Card lookup failed (code %u)\n", mysql_stmt_errno(stmt));
    mysql_stmt_close(stmt);
    return result;
}

static int db_record_card(MYSQL *db, const char *locker, const char *uid,
                          const char *request, int household_id, const char *status)
{
    MYSQL_STMT *stmt = mysql_stmt_init(db);
    if (!stmt) return -1;
    const char query[] =
        "INSERT INTO card_events (locker_id,uid,request_id,household_id,auth_result) "
        "VALUES (?,?,?,?,?) ON DUPLICATE KEY UPDATE "
        "household_id=VALUES(household_id),auth_result=VALUES(auth_result),"
        "received_count=received_count+1,last_received_at=CURRENT_TIMESTAMP(6)";
    MYSQL_BIND bind[5] = {{0}};
    const char *strings[5] = {locker, uid, request, NULL, status};
    unsigned long lengths[5] = {0};
    my_bool house_null = household_id == 0;
    for (int i = 0; i < 5; ++i) {
        if (i == 3) continue;
        lengths[i] = (unsigned long)strlen(strings[i]);
        bind[i].buffer_type = MYSQL_TYPE_STRING;
        bind[i].buffer = (void *)strings[i];
        bind[i].buffer_length = lengths[i];
        bind[i].length = &lengths[i];
    }
    bind[3].buffer_type = MYSQL_TYPE_LONG;
    bind[3].buffer = &household_id;
    bind[3].is_null = &house_null;
    int result = -1;
    if (!mysql_stmt_prepare(stmt, query, sizeof(query) - 1U) &&
        !mysql_stmt_bind_param(stmt, bind) && !mysql_stmt_execute(stmt)) result = 0;
    if (result < 0) fprintf(stderr, "Card record failed (code %u)\n", mysql_stmt_errno(stmt));
    mysql_stmt_close(stmt);
    return result;
}

/* Commit the automatic record before granting authorization. */
static const char *process_card(const char *locker, const char *uid, const char *request)
{
    last_approved_room[0] = '\0';
    if (!rental_valid_id(locker) || !rental_valid_uid(uid) || !rental_valid_request(request)) return "ERROR";
    MYSQL *db = db_connect();
    if (!db) return "ERROR";
    int house = 0;
    char room[11];
    const char *status = "ERROR";
    if (mysql_autocommit(db, 0) != 0) goto failed;
    int lookup = db_lookup_card(db, uid, &house, room);
    if (lookup < 0) goto failed;
    status = lookup == 1 ? "APPROVED" : "DENIED";
    if (db_record_card(db, locker, uid, request, house, status) != 0 || mysql_commit(db) != 0) goto failed;
    if (lookup == 1) strcpy(last_approved_room, room);
    printf("DB saved: locker=%s uid=%s request=%s result=%s household_id=%d\n",
           locker, uid, request, status, house);
    mysql_close(db);
    return status;
failed:
    mysql_rollback(db);
    mysql_close(db);
    fputs("DB processing failed; authorization refused\n", stderr);
    return "ERROR";
}

static int process_sensor(const char *locker, unsigned int adc, const char *request)
{
    if (!rental_valid_id(locker) || adc > 4095U || !rental_valid_request(request)) return -1;
    MYSQL *db = db_connect();
    if (!db) return -1;
    MYSQL_STMT *stmt = mysql_stmt_init(db);
    if (!stmt) { mysql_close(db); return -1; }
    const char query[] = "INSERT INTO cds_samples (locker_id,sample_id,adc_value) VALUES (?,?,?) "
                         "ON DUPLICATE KEY UPDATE adc_value=VALUES(adc_value),"
                         "last_received_at=CURRENT_TIMESTAMP(6)";
    MYSQL_BIND bind[3] = {{0}};
    unsigned long lengths[2] = {(unsigned long)strlen(locker), (unsigned long)strlen(request)};
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (void *)locker;
    bind[0].buffer_length = lengths[0];
    bind[0].length = &lengths[0];
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (void *)request;
    bind[1].buffer_length = lengths[1];
    bind[1].length = &lengths[1];
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = &adc;
    bind[2].is_unsigned = 1;
    int result = -1;
    if (!mysql_stmt_prepare(stmt, query, sizeof(query) - 1U) &&
        !mysql_stmt_bind_param(stmt, bind) && !mysql_stmt_execute(stmt)) result = 0;
    if (result < 0) fprintf(stderr, "CDS save failed (code %u)\n", mysql_stmt_errno(stmt));
    else printf("DB saved: locker=%s ADC=%u sample=%s\n", locker, adc, request);
    mysql_stmt_close(stmt);
    mysql_close(db);
    return result;
}

static int send_all(int fd, const char *line)
{
    size_t n = strlen(line);
    while (n) {
        ssize_t count = send(fd, line, n, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        line += (size_t)count;
        n -= (size_t)count;
    }
    return 0;
}

static const char *process_detection(const char *locker, const char *uid, const char *session,
                                     const char *camera, const char *label, unsigned int confidence, const char *event)
{
    if (!rental_valid_id(locker) || !rental_valid_uid(uid) || !rental_valid_request(session) ||
        !rental_valid_id(camera) || strncmp(camera, "CAMERA_", 7) || strcmp(camera + 7, locker) ||
        !rental_valid_id(label) || !strcmp(label, "NULL") || confidence < 900 || confidence > 1000 ||
        !rental_valid_request(event)) return "REJECTED";
    MYSQL *db = db_connect();
    if (!db) return "ERROR";
    MYSQL_STMT *stmt = NULL;
    const char *result = "ERROR";
    int house = 0;
    char room[11];
    if (mysql_autocommit(db, 0)) goto done;
    int allowed = db_lookup_card(db, uid, &house, room);
    if (allowed < 0) goto done;
    if (allowed == 0) { result = "REJECTED"; goto done; }
    stmt = mysql_stmt_init(db);
    if (!stmt) goto done;
    const char insert[] = "INSERT INTO tool_detections "
        "(locker_id,camera_id,uid,session_id,tool_label,confidence_milli,event_id,household_id) "
        "VALUES (?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE id=id";
    MYSQL_BIND bind[8] = {{0}};
    unsigned long lengths[8] = {0};
    const char *strings[8] = {locker, camera, uid, session, label, NULL, event, NULL};
    for (int i = 0; i < 8; ++i) {
        if (i == 5 || i == 7) continue;
        lengths[i] = (unsigned long)strlen(strings[i]);
        bind[i].buffer_type = MYSQL_TYPE_STRING;
        bind[i].buffer = (void *)strings[i];
        bind[i].buffer_length = lengths[i];
        bind[i].length = &lengths[i];
    }
    bind[5].buffer_type = MYSQL_TYPE_LONG;
    bind[5].buffer = &confidence;
    bind[5].is_unsigned = 1;
    bind[7].buffer_type = MYSQL_TYPE_LONG;
    bind[7].buffer = &house;
    if (mysql_stmt_prepare(stmt, insert, sizeof(insert) - 1U) || mysql_stmt_bind_param(stmt, bind) ||
        mysql_stmt_execute(stmt)) goto done;
    int new_detection = mysql_stmt_affected_rows(stmt) == 1;
    mysql_stmt_close(stmt);
    stmt = mysql_stmt_init(db);
    if (!stmt) goto done;
    const char verify[] = "SELECT COUNT(*) FROM tool_detections WHERE locker_id=? AND camera_id=? "
        "AND uid=? AND session_id=? AND tool_label=? AND confidence_milli=? AND event_id=? AND household_id=?";
    int matching = 0;
    MYSQL_BIND output[1] = {{0}};
    output[0].buffer_type = MYSQL_TYPE_LONG;
    output[0].buffer = &matching;
    if (mysql_stmt_prepare(stmt, verify, sizeof(verify) - 1U) || mysql_stmt_bind_param(stmt, bind) ||
        mysql_stmt_bind_result(stmt, output) || mysql_stmt_execute(stmt) || mysql_stmt_fetch(stmt))
        goto done;
    if (matching != 1) {
        fputs("Detection event ID already exists with different contents\n", stderr);
        mysql_stmt_close(stmt);
        stmt = NULL;
        goto done;
    }
    mysql_stmt_close(stmt);
    stmt = NULL;
    result = db_apply_rental(db, locker, session, camera, event, label, house, new_detection);
    if (!strcmp(result, "ERROR") || !strcmp(result, "REJECTED")) goto done;
    if (mysql_commit(db)) { result = "ERROR"; goto done; }
    printf("Detection saved: locker=%s uid=%s household_id=%d tool=%s confidence=%u/1000\n",
           locker, uid, house, label, confidence);
done:
    if (stmt) {
        if (!strcmp(result, "ERROR")) fprintf(stderr, "Detection SQL error (code %u)\n", mysql_stmt_errno(stmt));
        mysql_stmt_close(stmt);
    }
    if (!strcmp(result, "ERROR") || !strcmp(result, "REJECTED")) mysql_rollback(db);
    mysql_close(db);
    return result;
}

static int handle_message(int fd, const char *line)
{
    char locker[32], uid[21], request[17];
    const char *body;
    if (!rental_parse_frame(line, locker, &body)) return 0;
    if (strcmp(locker, "SERVER") == 0 && strcmp(body, "WORKER@READY") == 0) {
        puts("SQL_CLIENT ready - waiting for STM32 CARD messages");
        return 0;
    }
    if (strcmp(locker, "SERVER") && strcmp(locker, "SQL_CLIENT") && !strncmp(body, "DETECT@", 7)) {
        char session[17], camera[32], label[32], event[17];
        unsigned int confidence;
        if (!rental_parse_db_detect(body + 7, uid, session, camera, label, &confidence, event)) return 0;
        const char *status = process_detection(locker, uid, session, camera, label, confidence, event);
        char reply[112];
        snprintf(reply, sizeof(reply), "[%s]DETECT@%s@%s@%s\n", locker, camera, event, status);
        return send_all(fd, reply);
    }
    if (strcmp(locker, "SERVER") && strcmp(locker, "SQL_CLIENT") && strncmp(body, "SENSOR@", 7) == 0) {
        unsigned int adc;
        if (!rental_parse_sensor(body + 7, &adc, request)) return 0;
        const char *status = process_sensor(locker, adc, request) == 0 ? "SAVED" : "ERROR";
        char reply[96];
        snprintf(reply, sizeof(reply), "[%s]SENSOR@%s@%s\n", locker, request, status);
        return send_all(fd, reply);
    }
    if (strcmp(locker, "SERVER") == 0 || strcmp(locker, "SQL_CLIENT") == 0 ||
        strncmp(body, "CARD@", 5) || !rental_parse_card(body + 5, uid, request)) {
        puts("Unsupported/malformed message ignored");
        return 0;
    }
    const char *status = process_card(locker, uid, request);
    char reply[128];
    if (strcmp(status, "APPROVED") == 0) {
        snprintf(reply, sizeof(reply), "[%s]USER@%s@%s@%s\n", locker, uid, request, last_approved_room);
        if (send_all(fd, reply) != 0) return -1;
    }
    snprintf(reply, sizeof(reply), "[%s]AUTH@%s@%s@%s\n", locker, uid, request, status);
    if (send_all(fd, reply) != 0) return -1;
    printf("Relay TX: %s", reply);
    return 0;
}

static int receive_messages(int fd)
{
    char bytes[512], line[RENTAL_LINE_MAX + 1U];
    size_t used = 0;
    for (;;) {
        ssize_t count = recv(fd, bytes, sizeof(bytes), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        for (ssize_t i = 0; i < count; ++i) {
            char ch = bytes[i];
            if (ch == '\0' || (ch != '\n' && used >= RENTAL_LINE_MAX - 1U)) return -1;
            if (ch == '\n') {
                if (used && line[used - 1U] == '\r') --used;
                line[used] = '\0';
                if (handle_message(fd, line) != 0) return -1;
                used = 0;
            } else line[used++] = ch;
        }
    }
}

int main(int argc, char **argv)
{
    if ((argc != 3 && argc != 4) || (argc == 4 && strcmp(argv[3], "SQL_CLIENT"))) {
        fprintf(stderr, "Usage: %s <relay IPv4> <port> [SQL_CLIENT]\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *end;
    errno = 0;
    long port = strtol(argv[2], &end, 10);
    struct sockaddr_in address = {.sin_family = AF_INET};
    if (errno || end == argv[2] || *end || port < 1 || port > 65535 ||
        inet_pton(AF_INET, argv[1], &address.sin_addr) != 1) return EXIT_FAILURE;
    address.sin_port = htons((unsigned short)port);
    if (!getenv("DB_PASSWORD") || !*getenv("DB_PASSWORD")) {
        fputs("Set DB_PASSWORD before starting sql_client\n", stderr);
        return EXIT_FAILURE;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) return EXIT_FAILURE;
    setvbuf(stdout, NULL, _IOLBF, 0);
    puts("sql_client C - automatic card lookup and DB recording");
    for (;;) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { perror("socket"); return EXIT_FAILURE; }
        struct timeval timeout = {.tv_sec = 2};
        int keepalive = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) ||
            setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive))) {
            perror("socket options"); close(fd); return EXIT_FAILURE;
        }
        if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0 &&
            send_all(fd, "[SQL_CLIENT]HELLO\n") == 0) {
            puts("Connected to relay");
            receive_messages(fd);
        } else perror("relay connect/send");
        close(fd);
        puts("Relay disconnected; retry in 1 second");
        struct timespec delay = {.tv_sec = 1};
        while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
    }
}
