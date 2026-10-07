/* Pi C bridge: relay TCP <-> HC-05 RFCOMM serial device. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "rental_protocol.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_MAX 8192U
typedef struct { int fd; char bytes[QUEUE_MAX]; size_t length; const char *label; } output_t;
typedef struct { char bytes[RENTAL_LINE_MAX + 1]; size_t length; } input_t;

static int queue(output_t *target, const char *line)
{
    size_t n = strlen(line);
    if (n > sizeof(target->bytes) - target->length) { errno = ENOBUFS; return -1; }
    memcpy(target->bytes + target->length, line, n);
    target->length += n;
    return 0;
}
static int flush(output_t *target)
{
    if (!target->length) return 0;
    ssize_t n = write(target->fd, target->bytes, target->length);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    if (n <= 0) { if (n == 0) errno = EIO; return -1; }
    printf("[BRIDGE] %s wrote %zd bytes\n", target->label ? target->label : "output", n);
    target->length -= (size_t)n;
    memmove(target->bytes, target->bytes + n, target->length);
    return 0;
}
static int relay_line(output_t *serial, const char *line)
{
    char id[32];
    const char *body;
    if (!rental_parse_frame(line, id, &body) || rental_alarm_status(body) < 0) return 0;
    char framed[96];
    snprintf(framed, sizeof(framed), "[%s]%s\n", id, body);
    printf("HC-05 queued: %s", framed);
    return queue(serial, framed);
}
static int arduino_line(output_t *relay, const char *line)
{
    if (strcmp(line, "[BT_CLIENT]READY") == 0) return queue(relay, "[BT_CLIENT]READY\n");
    if (strncmp(line, "[BT_CLIENT]ACK@", 15) == 0) {
        char framed[RENTAL_LINE_MAX + 2];
        snprintf(framed, sizeof(framed), "%s\n", line);
        printf("Arduino RX: %s", framed);
        return queue(relay, framed);
    }
    return 0;
}
static int receive_lines(int fd, input_t *input, output_t *other, int from_relay)
{
    char data[512];
    ssize_t n = read(fd, data, sizeof(data));
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    /* A nonblocking tty may return zero while no bytes are available.
       Actual device hangup is detected through poll, not this idle read. */
    if (n == 0 && !from_relay) {
        struct timespec idle = {.tv_nsec = 1000000};
        nanosleep(&idle, NULL);
        return 0;
    }
    if (n <= 0) { if (n == 0) errno = ENOTCONN; return -1; }
    if (!from_relay) printf("[BRIDGE] HC-05 RX: %zd bytes\n", n);
    for (ssize_t i = 0; i < n; ++i) {
        char ch = data[i];
        if (!ch || (ch != '\n' && input->length >= RENTAL_LINE_MAX - 1U)) {
            errno = EPROTO;
            return -1;
        }
        if (ch == '\n') {
            if (input->length && input->bytes[input->length - 1U] == '\r') --input->length;
            input->bytes[input->length] = '\0';
            int result = from_relay ? relay_line(other, input->bytes) : arduino_line(other, input->bytes);
            input->length = 0;
            if (result != 0) return -1;
        } else input->bytes[input->length++] = ch;
    }
    return 0;
}
static int open_serial(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int error = errno;
        close(fd); errno = error; return -1;
    }
    struct termios config;
    if (tcgetattr(fd, &config)) { close(fd); return -1; }
    cfmakeraw(&config);
    config.c_cflag |= CLOCAL | CREAD;
    config.c_cflag &= ~(CRTSCTS | HUPCL);
    config.c_cc[VMIN] = 1;
    config.c_cc[VTIME] = 0;
    if (cfsetispeed(&config, B9600) || cfsetospeed(&config, B9600) || tcsetattr(fd, TCSANOW, &config)) {
        close(fd); return -1;
    }
    return fd;
}
static void bridge(int relay, int serial)
{
    output_t relay_output = {.fd = relay, .label = "Relay TCP"},
             serial_output = {.fd = serial, .label = "HC-05"};
    input_t relay_input = {{0}, 0}, serial_input = {{0}, 0};
    queue(&relay_output, "[BT_CLIENT]HELLO\n");
    for (;;) {
        struct pollfd descriptors[2] = {
            {relay, (short)(POLLIN | (relay_output.length ? POLLOUT : 0)), 0},
            {serial, (short)(POLLIN | (serial_output.length ? POLLOUT : 0)), 0}
        };
        int n = poll(descriptors, 2, 1000);
        if (n < 0) { if (errno == EINTR) continue; perror("[BRIDGE] poll"); return; }
        short bad = POLLERR | POLLHUP | POLLNVAL;
        if ((descriptors[0].revents | descriptors[1].revents) & bad) {
            if (descriptors[0].revents & bad)
                printf("[BRIDGE] Relay TCP hangup/error: poll=0x%X\n", (unsigned)descriptors[0].revents);
            if (descriptors[1].revents & bad)
                printf("[BRIDGE] HC-05 RFCOMM hangup/error: poll=0x%X\n", (unsigned)descriptors[1].revents);
            return;
        }
        if ((descriptors[0].revents & POLLIN) && receive_lines(relay, &relay_input, &serial_output, 1)) {
            perror("[BRIDGE] Relay TCP read"); return;
        }
        if ((descriptors[1].revents & POLLIN) && receive_lines(serial, &serial_input, &relay_output, 0)) {
            perror("[BRIDGE] HC-05 read"); return;
        }
        if ((descriptors[0].revents & POLLOUT) && flush(&relay_output)) {
            perror("[BRIDGE] Relay TCP write"); return;
        }
        if ((descriptors[1].revents & POLLOUT) && flush(&serial_output)) {
            perror("[BRIDGE] HC-05 write"); return;
        }
    }
}
int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <relay IPv4> <port> </dev/rfcomm0>\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *end;
    errno = 0;
    long port = strtol(argv[2], &end, 10);
    struct sockaddr_in address = {.sin_family = AF_INET};
    if (errno || end == argv[2] || *end || port < 1 || port > 65535 ||
        inet_pton(AF_INET, argv[1], &address.sin_addr) != 1) return EXIT_FAILURE;
    address.sin_port = htons((unsigned short)port);
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) return EXIT_FAILURE;
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (;;) {
        int serial = open_serial(argv[3]);
        if (serial >= 0) {
            int relay = socket(AF_INET, SOCK_STREAM, 0);
            if (relay >= 0) {
                if (connect(relay, (struct sockaddr *)&address, sizeof(address)) == 0 &&
                    fcntl(relay, F_SETFL, O_NONBLOCK) == 0) {
                    puts("Bluetooth bridge connected; waiting for alarm messages");
                    bridge(relay, serial);
                } else perror("relay connect");
                close(relay);
            }
            close(serial);
        } else perror("HC-05 serial open");
        puts("Bridge disconnected; retry in 1 second");
        struct timespec delay = {.tv_sec = 1};
        while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
    }
}
