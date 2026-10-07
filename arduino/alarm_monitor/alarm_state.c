#include "alarm_state.h"
#include "rental_protocol.h"
typedef struct { char id[32]; unsigned char active; } entry_t;
static entry_t entries[RENTAL_ALARM_LOCKERS];
static unsigned char fault;
void alarm_state_fault(void) { fault = 1; }
int alarm_state_handle(const char *line)
{
    char id[32];
    const char *body;
    if (!rental_parse_frame(line, id, &body)) return 0;
    int status = rental_alarm_status(body);
    if (status < 0) return 0;
    unsigned i;
    for (i = 0; i < RENTAL_ALARM_LOCKERS; ++i)
        if (!entries[i].id[0] || strcmp(entries[i].id, id) == 0) break;
    if (i == RENTAL_ALARM_LOCKERS) { fault = 1; return -1; }
    strcpy(entries[i].id, id);
    entries[i].active = (unsigned char)status;
    return 1;
}
int alarm_state_any_active(void)
{
    if (fault) return 1;
    for (unsigned i = 0; i < RENTAL_ALARM_LOCKERS; ++i) if (entries[i].active) return 1;
    return 0;
}
