/* Linux TCP relay. All database access is handled by sql_client.c. */
#define _POSIX_C_SOURCE 200809L
#include "rental_protocol.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_MAX 32
#define OUTPUT_MAX 8192U
#define REQUEST_TIMEOUT_MS 10000U
#define SESSION_TIMEOUT_MS 15000U

typedef struct {
    int fd, local, pending, sensor_pending;
    char id[32], line[RENTAL_LINE_MAX + 1U];
    size_t input_length;
    char output[OUTPUT_MAX];
    size_t output_length;
    uint64_t connected_at, requested_at, sensor_requested_at;
    char pending_uid[21], pending_request[17];
    char sensor_request[17];
    char camera_locker[32], detect_event[17], detect_session[17], detect_uid[21], detect_label[32];
    int detect_pending;
    uint64_t detect_requested_at;
} client_t;
static client_t clients[CLIENT_MAX];
static int sql_worker = -1;
static int bt_monitor = -1;
typedef struct { char id[32]; int active; } alarm_entry_t;
static alarm_entry_t alarm_entries[RENTAL_ALARM_LOCKERS];
typedef struct {
    char locker[32], approved_uid[21], approved_request[17], uid[21], session[17];
    uint64_t approved_at, reported_at;
    int active, online;
} session_entry_t;
static session_entry_t sessions[CLIENT_MAX];

static int alarm_snapshot(int monitor);
static int session_slot(const char *locker, int create);
static void session_notify(int slot);

static uint64_t now_ms(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)value.tv_sec * 1000U + (uint64_t)value.tv_nsec / 1000000U;
}

static int enqueue(int index, const char *line)
{
    client_t *client = &clients[index];
    size_t n = strlen(line);
    if (client->fd < 0 || n > sizeof(client->output) - client->output_length) return -1;
    memcpy(client->output + client->output_length, line, n);
    client->output_length += n;
    return 0;
}

static void close_client(int index)
{
    client_t *client = &clients[index];
    if (client->fd < 0) return;
    printf("Disconnected: %s\n", client->id[0] ? client->id : "unregistered");
    close(client->fd);
    client->fd = -1;
    client->pending = 0;
    client->sensor_pending = 0;
    client->detect_pending = 0;
    int session = session_slot(client->id, 0);
    if (session >= 0) { sessions[session].online = 0; session_notify(session); }
    if (bt_monitor == index) bt_monitor = -1;
    if (sql_worker == index) {
        sql_worker = -1;
        for (int i = 0; i < CLIENT_MAX; ++i) {
            if (clients[i].fd >= 0 && clients[i].pending) {
                char reply[96];
                snprintf(reply, sizeof(reply), "[SERVER]AUTH@%s@%s@ERROR\n",
                         clients[i].pending_uid, clients[i].pending_request);
                clients[i].pending = 0;
                if (enqueue(i, reply) != 0) close_client(i);
            }
            if (clients[i].fd >= 0 && clients[i].sensor_pending) {
                char reply[64];
                snprintf(reply, sizeof(reply), "[SERVER]SENSOR@%s@ERROR\n", clients[i].sensor_request);
                clients[i].sensor_pending = 0;
                if (enqueue(i, reply) != 0) close_client(i);
            }
            if (clients[i].fd >= 0 && clients[i].detect_pending) {
                char reply[64];
                snprintf(reply, sizeof(reply), "[SERVER]DETECT@%s@ERROR\n", clients[i].detect_event);
                clients[i].detect_pending = 0;
                if (enqueue(i, reply) != 0) close_client(i);
            }
        }
    }
}

static int session_slot(const char *locker, int create)
{
    int empty = -1;
    for (int i = 0; i < CLIENT_MAX; ++i) {
        if (sessions[i].locker[0] && strcmp(sessions[i].locker, locker) == 0) return i;
        if (!sessions[i].locker[0] && empty < 0) empty = i;
    }
    if (create && empty >= 0) { strcpy(sessions[empty].locker, locker); return empty; }
    return -1;
}
static int session_snapshot(int camera, int slot)
{
    session_entry_t *entry = &sessions[slot];
    int active = entry->active && entry->online && now_ms() - entry->reported_at < SESSION_TIMEOUT_MS;
    char message[128];
    snprintf(message, sizeof(message), "[SERVER]SESSION@%.31s@%s@%s@%s\n", entry->locker,
             active ? entry->uid : "NONE", active ? entry->session : "NONE", active ? "OPEN" : "CLOSED");
    return enqueue(camera, message);
}
static void session_notify(int slot)
{
    for (int i = 0; i < CLIENT_MAX; ++i)
        if (clients[i].fd >= 0 && clients[i].camera_locker[0] &&
            strcmp(clients[i].camera_locker, sessions[slot].locker) == 0 && session_snapshot(i, slot)) close_client(i);
}

static int alarm_snapshot(int monitor)
{
    for (unsigned i = 0; i < RENTAL_ALARM_LOCKERS; ++i) {
        if (alarm_entries[i].id[0]) {
            char message[80];
            snprintf(message, sizeof(message), "[%.31s]ALARM@%s\n", alarm_entries[i].id,
                     alarm_entries[i].active ? "ACTIVE" : "CLEAR");
            if (enqueue(monitor, message) != 0) return -1;
        }
    }
    return 0;
}

static int handle_message(int index, const char *line)
{
    client_t *client = &clients[index];
    char id[32];
    const char *body;
    if (!rental_parse_frame(line, id, &body)) return 0;
    printf("RX %s: %s\n", client->id[0] ? client->id : "new", line);
    if (strcmp(body, "HELLO") == 0) {
        if (strcmp(id, "SERVER") == 0) return -1;
        if (client->id[0] && strcmp(client->id, id)) return -1;
        for (int i = 0; i < CLIENT_MAX; ++i)
            if (i != index && clients[i].fd >= 0 && strcmp(clients[i].id, id) == 0) return -1;
        if (strcmp(id, "SQL_CLIENT") == 0) {
            /* Only the local Pi DB worker can register under this reserved name. */
            if (!client->local || (sql_worker >= 0 && sql_worker != index)) return -1;
            sql_worker = index;
            strcpy(client->id, id);
            puts("SQL_CLIENT registered - DB processing available");
            return enqueue(index, "[SERVER]WORKER@READY\n");
        }
        if (strcmp(id, "BT_CLIENT") == 0) {
            if (!client->local || (bt_monitor >= 0 && bt_monitor != index)) return -1;
            bt_monitor = index;
            strcpy(client->id, id);
            puts("BT_CLIENT registered - management-room alarm monitor");
            if (enqueue(index, "[SERVER]MONITOR@READY\n") != 0) return -1;
            return alarm_snapshot(index);
        }
        if (strncmp(id, "CAMERA_", 7) == 0) {
            if (!rental_valid_id(id + 7)) return -1;
            int slot = session_slot(id + 7, 1);
            if (slot < 0) return -1;
            strcpy(client->id, id);
            strcpy(client->camera_locker, id + 7);
            if (enqueue(index, "[SERVER]CAMERA@READY\n")) return -1;
            return session_snapshot(index, slot);
        }
        strcpy(client->id, id);
        int slot = session_slot(id, 1);
        if (slot < 0) return -1;
        sessions[slot].online = 0; /* Wait for the STM32's physical-control session report. */
        session_notify(slot);
        return enqueue(index, "[SERVER]PLUG@ON\n");
    }
    if (!client->id[0]) return -1;
    if (client->camera_locker[0]) {
        if (strcmp(id, client->id) || strncmp(body, "DETECT@", 7)) return 0;
        char session[17], label[32], event[17], reply[96];
        unsigned int confidence;
        if (!rental_parse_detect(body + 7, session, label, &confidence, event)) return 0;
        int slot = session_slot(client->camera_locker, 0);
        if (slot < 0 || !sessions[slot].active || !sessions[slot].online ||
            now_ms() - sessions[slot].reported_at >= SESSION_TIMEOUT_MS ||
            strcmp(sessions[slot].session, session)) {
            snprintf(reply, sizeof(reply), "[SERVER]DETECT@%s@REJECTED\n", event);
            return enqueue(index, reply);
        }
        if (sql_worker < 0 || (client->detect_pending && strcmp(client->detect_event, event))) {
            snprintf(reply, sizeof(reply), "[SERVER]DETECT@%s@ERROR\n", event);
            return enqueue(index, reply);
        }
        strcpy(client->detect_event, event);
        strcpy(client->detect_session, session);
        strcpy(client->detect_uid, sessions[slot].uid);
        strcpy(client->detect_label, label);
        client->detect_pending = 1;
        client->detect_requested_at = now_ms();
        char forwarded[RENTAL_LINE_MAX + 1U];
        snprintf(forwarded, sizeof(forwarded), "[%s]DETECT@%s@%s@%s@%s@%u@%s\n",
                 client->camera_locker, client->detect_uid, session, client->id, label, confidence, event);
        if (enqueue(sql_worker, forwarded)) close_client(sql_worker);
        return 0;
    }
    if (index == bt_monitor) {
        if (strcmp(id, "BT_CLIENT") == 0 && strcmp(body, "READY") == 0)
            return alarm_snapshot(index);
        if (strcmp(id, "BT_CLIENT") == 0 && strncmp(body, "ACK@", 4) == 0)
            printf("Arduino acknowledgement: %s\n", body + 4);
        return 0;
    }
    if (index == sql_worker) {
        if (strncmp(body, "DETECT@", 7) == 0) {
            char camera[32], event[17], status[10];
            const char *fields = body + 7;
            if (!rental_field(&fields, camera, 32, 0) || !rental_valid_id(camera) ||
                !rental_field(&fields, event, 17, 0) || !rental_valid_request(event) ||
                !rental_field(&fields, status, 10, 1) ||
                !rental_detection_status(status)) return 0;
            for (int i = 0; i < CLIENT_MAX; ++i) {
                client_t *target = &clients[i];
                if (target->fd < 0 || strcmp(target->id, camera) || !target->detect_pending ||
                    strcmp(target->camera_locker, id) || strcmp(target->detect_event, event)) continue;
                char reply[128];
                snprintf(reply, sizeof(reply), "[SERVER]DETECT@%s@%s\n", event, status);
                target->detect_pending = 0;
                if (enqueue(i, reply)) close_client(i);
                int slot = session_slot(id, 0);
                if ((!strcmp(status, "SAVED") || !strcmp(status, "RENTED") ||
                     !strcmp(status, "RETURNED") || !strcmp(status, "BUSY")) &&
                    slot >= 0 && sessions[slot].active && sessions[slot].online &&
                    !strcmp(sessions[slot].session, target->detect_session) &&
                    !strcmp(sessions[slot].uid, target->detect_uid)) {
                    for (int j = 0; j < CLIENT_MAX; ++j) {
                        if (clients[j].fd >= 0 && !strcmp(clients[j].id, id)) {
                            snprintf(reply, sizeof(reply), "[SERVER]TOOL@%s@%s@%s@%s\n",
                                     target->detect_session, target->detect_label, event,
                                     !strcmp(status,"SAVED") ? "SEEN" : status);
                            if (enqueue(j, reply)) close_client(j);
                        }
                    }
                }
                return 0;
            }
            return 0;
        }
        if (strncmp(body, "USER@", 5) == 0) {
            char uid[21], request[17], room[11];
            if (!rental_parse_user(body + 5, uid, request, room)) return 0;
            for (int i = 0; i < CLIENT_MAX; ++i) {
                client_t *target = &clients[i];
                if (target->fd >= 0 && strcmp(target->id, id) == 0 && target->pending &&
                    strcmp(target->pending_uid, uid) == 0 && strcmp(target->pending_request, request) == 0 &&
                    now_ms() - target->requested_at < REQUEST_TIMEOUT_MS) {
                    char reply[96];
                    snprintf(reply, sizeof(reply), "[SERVER]USER@%s@%s@%s\n", uid, request, room);
                    if (enqueue(i, reply) != 0) close_client(i);
                    return 0;
                }
            }
            return 0;
        }
        if (strncmp(body, "SENSOR@", 7) == 0) {
            char request[17], status[6];
            if (!rental_parse_sensor_reply(body + 7, request, status)) return 0;
            for (int i = 0; i < CLIENT_MAX; ++i) {
                client_t *target = &clients[i];
                if (target->fd >= 0 && strcmp(target->id, id) == 0 && target->sensor_pending &&
                    strcmp(target->sensor_request, request) == 0 &&
                    now_ms() - target->sensor_requested_at < REQUEST_TIMEOUT_MS) {
                    char reply[64];
                    snprintf(reply, sizeof(reply), "[SERVER]SENSOR@%s@%s\n", request, status);
                    target->sensor_pending = 0;
                    if (enqueue(i, reply) != 0) close_client(i);
                    return 0;
                }
            }
            return 0;
        }
        char uid[21], request[17], status[9];
        if (strncmp(body, "AUTH@", 5) || !rental_parse_auth(body + 5, uid, request, status)) return 0;
        for (int i = 0; i < CLIENT_MAX; ++i) {
            client_t *target = &clients[i];
            if (target->fd >= 0 && strcmp(target->id, id) == 0 && target->pending &&
                strcmp(target->pending_uid, uid) == 0 &&
                strcmp(target->pending_request, request) == 0 &&
                now_ms() - target->requested_at < REQUEST_TIMEOUT_MS) {
                char reply[96];
                snprintf(reply, sizeof(reply), "[SERVER]AUTH@%s@%s@%s\n", uid, request, status);
                if (!strcmp(status, "APPROVED")) {
                    int slot = session_slot(id, 1);
                    if (slot >= 0) {
                        strcpy(sessions[slot].approved_uid, uid);
                        strcpy(sessions[slot].approved_request, request);
                        sessions[slot].approved_at = now_ms();
                    }
                }
                target->pending = 0;
                if (enqueue(i, reply) != 0) close_client(i);
                printf("Relayed AUTH to %s: %s\n", id, status);
                return 0;
            }
        }
        puts("Ignored unmatched or expired SQL_CLIENT response");
        return 0;
    }
    if (strcmp(id, "SERVER") == 0 && strcmp(body, "PLUG@ON") == 0)
        return enqueue(index, "[SERVER]PLUG@OFF\n");
    if (strcmp(id, "SERVER") == 0 && strcmp(body, "PLUG@OFF") == 0) {
        printf("PASS: HELLO and LED ON/OFF round trip (%s)\n", client->id);
        return 0;
    }
    if (strcmp(id, client->id)) return 0;
    if (strncmp(body, "SESSION@", 8) == 0) {
        int slot = session_slot(id, 1);
        if (slot < 0) return -1;
        session_entry_t *entry = &sessions[slot];
        if (strcmp(body + 8, "CLOSED") == 0) { entry->active = 0; entry->online = 1; }
        else {
            char uid[21], session[17], state[7];
            if (!rental_parse_session(body + 8, uid, session, state)) return 0;
            if (!strcmp(state, "OPEN")) {
                int resume = entry->active && !strcmp(entry->uid, uid) && !strcmp(entry->session, session);
                int authorized = !strcmp(entry->approved_uid, uid) && !strcmp(entry->approved_request, session) &&
                                 now_ms() - entry->approved_at < REQUEST_TIMEOUT_MS;
                if (!resume && !authorized) return enqueue(index, "[SERVER]SESSION@ERROR\n");
                strcpy(entry->uid, uid);
                strcpy(entry->session, session);
                entry->active = 1;
            } else {
                if (entry->active && (strcmp(entry->uid, uid) || strcmp(entry->session, session)))
                    return enqueue(index, "[SERVER]SESSION@ERROR\n");
                entry->active = 0;
            }
            entry->online = 1;
        }
        entry->reported_at = now_ms();
        session_notify(slot);
        return enqueue(index, "[SERVER]SESSION@RECEIVED\n");
    }
    int alarm = rental_alarm_status(body);
    if (alarm >= 0) {
        unsigned slot;
        for (slot = 0; slot < RENTAL_ALARM_LOCKERS; ++slot)
            if (!alarm_entries[slot].id[0] || strcmp(alarm_entries[slot].id, id) == 0) break;
        if (slot == RENTAL_ALARM_LOCKERS) {
            puts("Alarm registry full; rejecting connection");
            return -1;
        }
        strcpy(alarm_entries[slot].id, id);
        alarm_entries[slot].active = alarm;
        if (bt_monitor >= 0) {
            char message[80];
            snprintf(message, sizeof(message), "[%s]ALARM@%s\n", id, alarm ? "ACTIVE" : "CLEAR");
            if (enqueue(bt_monitor, message) != 0) close_client(bt_monitor);
        } else puts("No BT_CLIENT; alarm retained for management monitor reconnect");
        return enqueue(index, "[SERVER]ALARM@RECEIVED\n");
    }
    if (strncmp(body, "SENSOR@", 7) == 0) {
        unsigned int adc;
        char request[17], reply[96];
        if (!rental_parse_sensor(body + 7, &adc, request)) return 0;
        if (sql_worker < 0 || clients[sql_worker].fd < 0) {
            snprintf(reply, sizeof(reply), "[SERVER]SENSOR@%s@ERROR\n", request);
            return enqueue(index, reply);
        }
        strcpy(client->sensor_request, request);
        client->sensor_pending = 1;
        client->sensor_requested_at = now_ms();
        snprintf(reply, sizeof(reply), "[%s]SENSOR@%u@%s\n", client->id, adc, request);
        if (enqueue(sql_worker, reply) != 0) close_client(sql_worker);
        return 0;
    }
    if (strncmp(body, "CARD@", 5)) return 0;
    char uid[21], request[17], reply[96];
    if (!rental_parse_card(body + 5, uid, request)) {
        /* UID-only legacy messages get a receipt, never authorization. */
        if (rental_valid_uid(body + 5)) {
            snprintf(reply, sizeof(reply), "[SERVER]CARD@%s@RECEIVED\n", body + 5);
            return enqueue(index, reply);
        }
        puts("Invalid card frame; ignored");
        return 0;
    }
    snprintf(reply, sizeof(reply), "[SERVER]CARD@%s@%s@RECEIVED\n", uid, request);
    if (enqueue(index, reply) != 0) return -1;
    if (sql_worker < 0 || clients[sql_worker].fd < 0) {
        snprintf(reply, sizeof(reply), "[SERVER]AUTH@%s@%s@ERROR\n", uid, request);
        puts("No SQL_CLIENT available; authorization refused");
        return enqueue(index, reply);
    }
    strcpy(client->pending_uid, uid);
    strcpy(client->pending_request, request);
    client->requested_at = now_ms();
    client->pending = 1;
    char forwarded[RENTAL_LINE_MAX + 1U];
    snprintf(forwarded, sizeof(forwarded), "[%s]CARD@%s@%s\n", client->id, uid, request);
    if (enqueue(sql_worker, forwarded) != 0) close_client(sql_worker);
    else printf("Forwarded CARD to SQL_CLIENT: %s\n", client->id);
    return 0;
}

static int receive_client(int index)
{
    client_t *client = &clients[index];
    char bytes[512];
    ssize_t count = recv(client->fd, bytes, sizeof(bytes), 0);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    if (count <= 0) return -1;
    for (ssize_t i = 0; i < count; ++i) {
        char ch = bytes[i];
        if (ch == '\0' || (ch != '\n' && client->input_length >= RENTAL_LINE_MAX - 1U)) return -1;
        if (ch == '\n') {
            if (client->input_length && client->line[client->input_length - 1U] == '\r')
                --client->input_length;
            client->line[client->input_length] = '\0';
            if (handle_message(index, client->line) != 0) return -1;
            client->input_length = 0;
        } else client->line[client->input_length++] = ch;
    }
    return 0;
}

static int flush_client(int index)
{
    client_t *client = &clients[index];
    if (!client->output_length) return 0;
    ssize_t count = send(client->fd, client->output, client->output_length, 0);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    if (count <= 0) return -1;
    memmove(client->output, client->output + count, client->output_length - (size_t)count);
    client->output_length -= (size_t)count;
    return 0;
}

int main(int argc, char **argv)
{
    long port = 5000;
    if (argc != 1) {
        if (argc != 3 || strcmp(argv[1], "--port")) {
            fprintf(stderr, "Usage: %s [--port 5000] (DB is handled by sql_client)\n", argv[0]);
            return EXIT_FAILURE;
        }
        char *end;
        errno = 0;
        port = strtol(argv[2], &end, 10);
        if (errno || end == argv[2] || *end || port < 1 || port > 65535) return EXIT_FAILURE;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) return EXIT_FAILURE;
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (int i = 0; i < CLIENT_MAX; ++i) clients[i].fd = -1;
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { perror("socket"); return EXIT_FAILURE; }
    int reuse = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) ||
        fcntl(listener, F_SETFL, O_NONBLOCK) < 0) {
        perror("listener options"); close(listener); return EXIT_FAILURE;
    }
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((unsigned short)port),
                                  .sin_addr = {.s_addr = htonl(INADDR_ANY)}};
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, CLIENT_MAX)) {
        perror("bind/listen"); close(listener); return EXIT_FAILURE;
    }
    printf("wifi_test_server C v11 - automatic rentals + returns; Listening on port %ld\n", port);
    for (;;) {
        struct pollfd descriptors[CLIENT_MAX + 1];
        descriptors[0] = (struct pollfd){listener, POLLIN, 0};
        for (int i = 0; i < CLIENT_MAX; ++i)
            descriptors[i + 1] = (struct pollfd){clients[i].fd,
                (short)(POLLIN | (clients[i].output_length ? POLLOUT : 0)), 0};
        int polled = poll(descriptors, CLIENT_MAX + 1, 200);
        if (polled < 0) { if (errno == EINTR) continue; perror("poll"); break; }
        for (int i = 0; i < CLIENT_MAX; ++i) {
            short event = descriptors[i + 1].revents;
            if (clients[i].fd < 0 || descriptors[i + 1].fd != clients[i].fd) continue;
            if ((event & (POLLERR | POLLHUP | POLLNVAL)) ||
                ((event & POLLIN) && receive_client(i) != 0) ||
                ((event & POLLOUT) && flush_client(i) != 0)) close_client(i);
        }
        if (descriptors[0].revents & POLLIN) {
            struct sockaddr_in peer;
            socklen_t size = sizeof(peer);
            int fd = accept(listener, (struct sockaddr *)&peer, &size);
            if (fd >= 0) {
                int slot;
                for (slot = 0; slot < CLIENT_MAX; ++slot) if (clients[slot].fd < 0) break;
                if (slot == CLIENT_MAX || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) close(fd);
                else {
                    memset(&clients[slot], 0, sizeof(clients[slot]));
                    clients[slot].fd = fd;
                    clients[slot].local = ntohl(peer.sin_addr.s_addr) == INADDR_LOOPBACK;
                    clients[slot].connected_at = now_ms();
                    char ip[INET_ADDRSTRLEN];
                    const char *text = inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
                    printf("Connected: %s:%u\n", text ? text : "unknown", ntohs(peer.sin_port));
                }
            }
        }
        uint64_t now = now_ms();
        for (int i = 0; i < CLIENT_MAX; ++i) {
            if (sessions[i].active && sessions[i].online && now - sessions[i].reported_at >= SESSION_TIMEOUT_MS) {
                sessions[i].online = 0;
                session_notify(i);
            }
        }
        for (int i = 0; i < CLIENT_MAX; ++i) {
            client_t *client = &clients[i];
            if (client->fd < 0) continue;
            if (!client->id[0] && now - client->connected_at >= 5000U) { close_client(i); continue; }
            if (client->pending && now - client->requested_at >= REQUEST_TIMEOUT_MS) {
                char reply[96];
                snprintf(reply, sizeof(reply), "[SERVER]AUTH@%s@%s@ERROR\n",
                         client->pending_uid, client->pending_request);
                client->pending = 0;
                if (enqueue(i, reply) != 0) close_client(i);
            }
            if (client->fd >= 0 && client->sensor_pending &&
                now - client->sensor_requested_at >= REQUEST_TIMEOUT_MS) {
                char reply[64];
                snprintf(reply, sizeof(reply), "[SERVER]SENSOR@%s@ERROR\n", client->sensor_request);
                client->sensor_pending = 0;
                if (enqueue(i, reply) != 0) close_client(i);
            }
            if (client->fd >= 0 && client->detect_pending && now - client->detect_requested_at >= REQUEST_TIMEOUT_MS) {
                char reply[64];
                snprintf(reply, sizeof(reply), "[SERVER]DETECT@%s@ERROR\n", client->detect_event);
                client->detect_pending = 0;
                if (enqueue(i, reply)) close_client(i);
            }
        }
    }
    for (int i = 0; i < CLIENT_MAX; ++i) close_client(i);
    close(listener);
    return EXIT_FAILURE;
}
