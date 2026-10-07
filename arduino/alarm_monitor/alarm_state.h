#ifndef ALARM_STATE_H
#define ALARM_STATE_H
int alarm_state_handle(const char *line);
int alarm_state_any_active(void);
void alarm_state_fault(void);
#endif
