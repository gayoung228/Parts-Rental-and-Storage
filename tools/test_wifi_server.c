/* Linux C integration test. Starts the server and simulates STM32 messages. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static pid_t server_pid = -1;

static void cleanup(void)
{
    if (server_pid > 0) {
        kill(server_pid, SIGTERM);
        while (waitpid(server_pid, NULL, 0) < 0 && errno == EINTR) {}
        server_pid = -1;
    }
}

static void require(int passed, const char *description)
{
    if (!passed) {
        fprintf(stderr, "FAIL: %s\n", description);
        exit(EXIT_FAILURE);
    }
}

static void pause_ms(long ms)
{
    struct timespec delay = {ms / 1000, (ms % 1000) * 1000000L};
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
}

static int connect_client(unsigned short port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    require(fd >= 0, "client socket");
    struct sockaddr_in address = {
        .sin_family = AF_INET, .sin_port = htons(port),
        .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)}
    };
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        return -1;
    }
    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    require(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
            "receive timeout");
    require(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0,
            "send timeout");
    return fd;
}

static void send_bytes(int fd, const char *data, size_t length)
{
    while (length > 0) {
        ssize_t count = send(fd, data, length, 0);
        if (count < 0 && errno == EINTR) continue;
        require(count > 0, "send test request");
        data += (size_t)count;
        length -= (size_t)count;
    }
}

static void send_text(int fd, const char *text)
{
    send_bytes(fd, text, strlen(text));
}

static void expect_line(int fd, const char *expected)
{
    char line[256];
    size_t used = 0;
    while (used < sizeof(line) - 1U) {
        ssize_t count = recv(fd, line + used, 1, 0);
        if (count < 0 && errno == EINTR) continue;
        require(count == 1, "receive complete response");
        if (line[used++] == '\n') break;
    }
    line[used] = '\0';
    require(strcmp(line, expected) == 0, "response contents");
}

static void check_invalid_port(const char *binary, const char *port)
{
    pid_t child = fork();
    require(child >= 0, "fork port validation");
    if (child == 0) {
        execl(binary, binary, "--port", port, (char *)NULL);
        _exit(127);
    }
    int status;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    require(result == child && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE,
            "reject invalid port");
}

int main(int argc, char **argv)
{
    const char *binary = argc == 2 ? argv[1] : "./wifi_test_server";
    require(argc <= 2, "usage: test_wifi_server [server executable]");
    require(signal(SIGPIPE, SIG_IGN) != SIG_ERR, "ignore SIGPIPE");
    require(atexit(cleanup) == 0, "register server cleanup");
    const char *invalid_ports[] = {"0", "65536", "abc", "5000x"};
    for (size_t i = 0; i < sizeof(invalid_ports) / sizeof(invalid_ports[0]); ++i)
        check_invalid_port(binary, invalid_ports[i]);

    int probe = socket(AF_INET, SOCK_STREAM, 0);
    require(probe >= 0, "port probe socket");
    struct sockaddr_in address = {
        .sin_family = AF_INET, .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)}
    };
    require(bind(probe, (struct sockaddr *)&address, sizeof(address)) == 0, "find test port");
    socklen_t size = sizeof(address);
    require(getsockname(probe, (struct sockaddr *)&address, &size) == 0, "read test port");
    unsigned short port = ntohs(address.sin_port);
    close(probe);
    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    server_pid = fork();
    require(server_pid >= 0, "fork server");
    if (server_pid == 0) {
        execl(binary, binary, "--port", port_text, (char *)NULL);
        _exit(127);
    }
    int client = -1;
    for (int retry = 0; retry < 50 && client < 0; ++retry) {
        client = connect_client(port);
        if (client < 0) pause_ms(50);
    }
    require(client >= 0, "server startup");
    int idle_client = connect_client(port);
    require(idle_client >= 0, "idle second client must not block STM32");
    send_text(client, "[LOCKER_101]HEL");
    pause_ms(50);
    send_text(client, "LO\r\n");
    expect_line(client, "[SERVER]PLUG@ON\n");
    send_text(client, "[SERVER]PLUG@ON\n[SERVER]PLUG@OFF\n");
    expect_line(client, "[SERVER]PLUG@OFF\n");
    const char *uids[] = {"A1B2C3D4", "04A1B2C3D4E5F6", "0102030405060708090A"};
    for (size_t i = 0; i < sizeof(uids) / sizeof(uids[0]); ++i) {
        char request[64], response[64];
        snprintf(request, sizeof(request), "[LOCKER_101]CARD@%s\n", uids[i]);
        snprintf(response, sizeof(response), "[SERVER]CARD@%s@RECEIVED\n", uids[i]);
        send_text(client, request);
        expect_line(client, response);
    }
    send_text(client, "[LOCKER_101]CARD@INVALID\n[LOCKER_101]HELLO\n");
    expect_line(client, "[SERVER]PLUG@ON\n");
    send_text(client, "[LOCKER_101]CARD@E6044006@0000000100000002\n");
    expect_line(client, "[SERVER]CARD@E6044006@0000000100000002@RECEIVED\n");
    expect_line(client, "[SERVER]AUTH@E6044006@0000000100000002@ERROR\n");
    send_text(client, "[LOCKER_101]CARD@E6044006@BAD\n[LOCKER_101]HELLO\n");
    expect_line(client, "[SERVER]PLUG@ON\n");
    int worker = connect_client(port);
    require(worker >= 0, "connect SQL worker while STM32 is connected");
    send_text(worker, "[SQL_CLIENT]HELLO\n");
    expect_line(worker, "[SERVER]WORKER@READY\n");
    send_text(client, "[LOCKER_101]CARD@E6044006@0000000100000003\n");
    expect_line(client, "[SERVER]CARD@E6044006@0000000100000003@RECEIVED\n");
    expect_line(worker, "[LOCKER_101]CARD@E6044006@0000000100000003\n");
    send_text(worker, "[LOCKER_101]USER@E6044006@0000000100009999@999\n"
                      "[LOCKER_101]USER@E6044006@0000000100000003@101\n"
                      "[LOCKER_101]AUTH@E6044006@0000000100009999@APPROVED\n"
                      "[LOCKER_101]AUTH@E6044006@0000000100000003@APPROVED\n");
    expect_line(client, "[SERVER]USER@E6044006@0000000100000003@101\n");
    expect_line(client, "[SERVER]AUTH@E6044006@0000000100000003@APPROVED\n");
    int second = connect_client(port);
    require(second >= 0, "second locker connection");
    send_text(second, "[LOCKER_102]HELLO\n");
    expect_line(second, "[SERVER]PLUG@ON\n");
    send_text(second, "[LOCKER_102]CARD@D3693D06@0000000100000004\n");
    expect_line(second, "[SERVER]CARD@D3693D06@0000000100000004@RECEIVED\n");
    expect_line(worker, "[LOCKER_102]CARD@D3693D06@0000000100000004\n");
    send_text(worker, "[LOCKER_101]AUTH@D3693D06@0000000100000004@APPROVED\n"
                      "[LOCKER_102]AUTH@D3693D06@0000000100000004@DENIED\n");
    expect_line(second, "[SERVER]AUTH@D3693D06@0000000100000004@DENIED\n");
    send_text(client, "[LOCKER_101]SENSOR@3800@0000000100000005\n");
    expect_line(worker, "[LOCKER_101]SENSOR@3800@0000000100000005\n");
    send_text(worker, "[LOCKER_101]SENSOR@0000000100000005@SAVED\n");
    expect_line(client, "[SERVER]SENSOR@0000000100000005@SAVED\n");
    send_text(client, "[LOCKER_101]CARD@E6044006@0000000100000006\n");
    expect_line(client, "[SERVER]CARD@E6044006@0000000100000006@RECEIVED\n");
    expect_line(worker, "[LOCKER_101]CARD@E6044006@0000000100000006\n");
    close(worker);
    expect_line(client, "[SERVER]AUTH@E6044006@0000000100000006@ERROR\n");
    worker = connect_client(port);
    require(worker >= 0, "SQL worker reconnect");
    send_text(worker, "[SQL_CLIENT]HELLO\n");
    expect_line(worker, "[SERVER]WORKER@READY\n");
    close(worker);
    close(second);
    close(idle_client);
    /* Management alarm relay works independently of the SQL worker. */
    send_text(client, "[LOCKER_101]ALARM@ACTIVE\n");
    expect_line(client, "[SERVER]ALARM@RECEIVED\n");
    int monitor = connect_client(port);
    require(monitor >= 0, "management monitor connect");
    send_text(monitor, "[BT_CLIENT]HELLO\n");
    expect_line(monitor, "[SERVER]MONITOR@READY\n");
    expect_line(monitor, "[LOCKER_101]ALARM@ACTIVE\n");
    send_text(client, "[LOCKER_101]ALARM@CLEAR\n");
    expect_line(client, "[SERVER]ALARM@RECEIVED\n");
    expect_line(monitor, "[LOCKER_101]ALARM@CLEAR\n");
    send_text(monitor, "[BT_CLIENT]READY\n");
    expect_line(monitor, "[LOCKER_101]ALARM@CLEAR\n");
    send_text(monitor, "[BT_CLIENT]ACK@LOCKER_101@CLEAR\n");
    close(monitor);
    close(client);

    char oversized[257];
    memset(oversized, 'A', sizeof(oversized));
    const char nul_line[] = "hello\0\n";
    for (int i = 0; i < 2; ++i) {
        client = connect_client(port);
        require(client >= 0, "reconnect for invalid request");
        send_bytes(client, i == 0 ? oversized : nul_line,
                   i == 0 ? sizeof(oversized) : sizeof(nul_line) - 1U);
        char byte;
        ssize_t count = recv(client, &byte, 1, 0);
        require(count == 0 || (count < 0 && errno == ECONNRESET), "reject invalid line");
        close(client);
    }
    client = connect_client(port);
    require(client >= 0, "reconnect after rejected requests");
    send_bytes(client, oversized, 255);
    send_text(client, "\n[LOCKER_101]HELLO\n");
    expect_line(client, "[SERVER]PLUG@ON\n");
    close(client);
    puts("PASS: invalid ports, fragmented HELLO, multiple lines, LED round trip,");
    puts("      concurrent lockers, SQL routing, wrong response IDs, CDS and worker reconnect");
    return EXIT_SUCCESS;
}
