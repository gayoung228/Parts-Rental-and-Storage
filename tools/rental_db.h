#ifndef RENTAL_DB_H
#define RENTAL_DB_H
/* Invoked within the detection transaction. Caller commits observation + action together. */
static const char *db_apply_rental(MYSQL *db, const char *locker, const char *session,
                                 const char *camera, const char *event, const char *label,
                                 int house, int new_detection)
{
    char escaped[4][65], query[1024];
    const char *values[4] = {locker, session, camera, event};
    for (int i=0;i<4;++i) mysql_real_escape_string(db,escaped[i],values[i],(unsigned long)strlen(values[i]));
    /* Labels have already passed rental_valid_id: SQL metacharacters are disallowed. */
    snprintf(query,sizeof(query),"SELECT id,enabled FROM rental_tools WHERE tool_no='%s' FOR UPDATE",label);
    if (mysql_query(db,query)) goto error;
    MYSQL_RES *rows=mysql_store_result(db);
    if (!rows) goto error;
    MYSQL_ROW row=mysql_fetch_row(rows);
    if (!row) { mysql_free_result(rows); return new_detection ? "REJECTED" : "SAVED"; }
    unsigned long long tool=strtoull(row[0],NULL,10);
    int enabled=atoi(row[1]);
    mysql_free_result(rows);
    snprintf(query,sizeof(query),"SELECT household_id,camera_id,event_id,action FROM rental_actions "
             "WHERE locker_id='%s' AND session_id='%s' AND tool_id=%llu",escaped[0],escaped[1],tool);
    if (mysql_query(db,query)) goto error;
    rows=mysql_store_result(db);
    if (!rows) goto error;
    row=mysql_fetch_row(rows);
    if (row) {
        const char *result="UNCHANGED";
        if (atoi(row[0])!=house) result="REJECTED";
        else if (!strcmp(row[1],camera) && !strcmp(row[2],event)) {
            if (!strcmp(row[3],"RENTED")) result="RENTED";
            else if (!strcmp(row[3],"RETURNED")) result="RETURNED";
            else result="BUSY";
        }
        mysql_free_result(rows);
        return result;
    }
    mysql_free_result(rows);
    /* Existing observations from before this feature are never replayed as rentals. */
    if (!new_detection) return "SAVED";
    if (!enabled) return "REJECTED";
    snprintf(query,sizeof(query),"SELECT id,household_id FROM loans WHERE tool_id=%llu "
             "AND return_date IS NULL FOR UPDATE",tool);
    if (mysql_query(db,query)) goto error;
    rows=mysql_store_result(db);
    if (!rows) goto error;
    row=mysql_fetch_row(rows);
    unsigned long long loan=row ? strtoull(row[0],NULL,10) : 0;
    int owner=row ? atoi(row[1]) : 0;
    mysql_free_result(rows);
    const char *action;
    if (loan && owner!=house) action="BUSY";
    else if (loan) {
        snprintf(query,sizeof(query),"UPDATE loans SET return_date=UTC_TIMESTAMP(6)+INTERVAL 9 HOUR "
                 "WHERE id=%llu AND return_date IS NULL",loan);
        if (mysql_query(db,query) || mysql_affected_rows(db)!=1) goto error;
        action="RETURNED";
    } else {
        snprintf(query,sizeof(query),"INSERT INTO loans(household_id,tool_id,rental_date,due_date,"
                 "rental_fee_won,daily_late_fee_won,late_fee_period_seconds) "
                 "SELECT %d,%llu,UTC_TIMESTAMP(6)+INTERVAL 9 HOUR,"
                 "TIMESTAMPADD(SECOND,duration_seconds,UTC_TIMESTAMP(6)+INTERVAL 9 HOUR),"
                 "rental_fee_won,late_fee_won,late_fee_period_seconds FROM rental_policy WHERE id=1",house,tool);
        if (mysql_query(db,query) || mysql_affected_rows(db)!=1) goto error;
        loan=mysql_insert_id(db);
        action="RENTED";
    }
    char loan_value[32];
    if (loan) snprintf(loan_value,sizeof(loan_value),"%llu",loan);
    else strcpy(loan_value,"NULL");
    snprintf(query,sizeof(query),"INSERT INTO rental_actions(locker_id,session_id,tool_id,household_id,"
             "camera_id,event_id,action,loan_id) VALUES('%s','%s',%llu,%d,'%s','%s','%s',%s)",
             escaped[0],escaped[1],tool,house,escaped[2],escaped[3],action,loan_value);
    if (mysql_query(db,query)) goto error;
    printf("Rental %s: household_id=%d tool=%s loan=%llu\n",action,house,label,loan);
    return action;
error:
    fprintf(stderr,"Rental SQL error (code %u)\n",mysql_errno(db));
    return "ERROR";
}
#endif
