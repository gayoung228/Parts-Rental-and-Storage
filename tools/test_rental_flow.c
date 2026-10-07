/* Fixture-only integration: schemas/account seeded by the temporary DB runner. */
#define main sql_client_program_main
#include "sql_client.c"
#undef main
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static MYSQL *admin;
static void sql(const char *query) { if(mysql_query(admin,query)) { fprintf(stderr,"%s\n",mysql_error(admin)); exit(1); } }
static long scalar(const char *query) {
    sql(query); MYSQL_RES *res=mysql_store_result(admin); CHECK(res);
    MYSQL_ROW row=mysql_fetch_row(res); CHECK(row&&row[0]);
    long value=strtol(row[0],NULL,10); mysql_free_result(res); return value;
}
static void detection(const char *uid,const char *session,const char *label,const char *event,const char *result) {
    CHECK(!strcmp(process_detection("LOCKER_101",uid,session,"CAMERA_LOCKER_101",label,950,event),result));
}
int main(void) {
    CHECK(getenv("DB_SOCKET")); admin=mysql_init(NULL); CHECK(admin);
    CHECK(mysql_real_connect(admin,"localhost","root","","parts_rental",0,getenv("DB_SOCKET"),0));
    sql("INSERT INTO households(id,building_no,unit_no) VALUES(1,'101','101'),(2,'101','102')");
    sql("INSERT INTO rfid_cards(uid,household_id,enabled) VALUES('E6044006',1,1),('D3693D06',2,1)");
    CHECK(setenv("DB_PASSWORD","temporary-test-password",1)==0);
    detection("E6044006","0000000100000001","1","0000000100000001","RENTED");
    CHECK(scalar("SELECT rental_state FROM rental_tool_status WHERE tool_no='1'")==1);
    CHECK(scalar("SELECT TIMESTAMPDIFF(SECOND,rental_date,due_date) FROM loans WHERE tool_id=1")==300);
    CHECK(scalar("SELECT amount_due_won FROM household_rental_summary WHERE household_id=1")==500);
    detection("E6044006","0000000100000001","1","0000000100000001","RENTED");
    detection("E6044006","0000000100000001","1","0000000100000002","UNCHANGED");
    CHECK(scalar("SELECT COUNT(*) FROM loans")==1);
    detection("E6044006","0000000100000001","2","0000000100000001","ERROR");
    CHECK(scalar("SELECT COUNT(*) FROM tool_detections")==2);
    detection("D3693D06","0000000100000002","1","0000000100000003","BUSY");
    CHECK(scalar("SELECT rental_state FROM rental_tool_status WHERE tool_no='1'")==1);
    detection("E6044006","0000000100000003","1","0000000100000004","RETURNED");
    CHECK(scalar("SELECT rental_state FROM rental_tool_status WHERE tool_no='1'")==0);
    CHECK(scalar("SELECT unreturned_count FROM household_rental_summary WHERE household_id=1")==0);
    CHECK(scalar("SELECT amount_due_won FROM household_rental_summary WHERE household_id=1")==500);
    detection("E6044006","0000000100000003","1","0000000100000005","UNCHANGED");
    detection("D3693D06","0000000100000004","1","0000000100000006","RENTED");
    CHECK(scalar("SELECT household_id FROM rental_tool_status WHERE tool_no='1'")==2);
    /* Returned timestamps allow exact, deterministic boundary tests. */
    sql("UPDATE loans SET due_date=rental_date+INTERVAL 300 SECOND,return_date=rental_date+INTERVAL 300 SECOND WHERE id=1");
    CHECK(scalar("SELECT assessed_amount_won FROM loan_list WHERE loan_id=1")==500);
    sql("UPDATE loans SET return_date=due_date+INTERVAL 1 MICROSECOND WHERE id=1");
    CHECK(scalar("SELECT assessed_amount_won FROM loan_list WHERE loan_id=1")==1000);
    sql("UPDATE loans SET return_date=due_date+INTERVAL 300 SECOND WHERE id=1");
    CHECK(scalar("SELECT assessed_amount_won FROM loan_list WHERE loan_id=1")==1000);
    sql("UPDATE loans SET return_date=due_date+INTERVAL 300 SECOND+INTERVAL 1 MICROSECOND WHERE id=1");
    CHECK(scalar("SELECT assessed_amount_won FROM loan_list WHERE loan_id=1")==1500);
    sql("INSERT INTO loan_billing_items(loan_id,billing_month,amount_won) VALUES(1,'2026-10-01',500)");
    CHECK(scalar("SELECT amount_due_won FROM household_rental_summary WHERE household_id=1")==1000);
    sql("UPDATE loans SET due_date=UTC_TIMESTAMP(6)+INTERVAL 9 HOUR-INTERVAL 1 SECOND,rental_date=UTC_TIMESTAMP(6)+INTERVAL 9 HOUR-INTERVAL 301 SECOND WHERE id=2");
    CHECK(scalar("SELECT assessed_amount_won FROM loan_list WHERE loan_id=2")==1000);
    detection("D3693D06","0000000100000005","1","0000000100000007","RETURNED");
    CHECK(scalar("SELECT assessed_amount_won FROM loan_list WHERE loan_id=2")==1000);
    detection("E6044006","0000000100000006","64","0000000100000008","REJECTED");
    sql("RENAME TABLE rental_actions TO unavailable_actions");
    long before=scalar("SELECT COUNT(*) FROM tool_detections");
    detection("E6044006","0000000100000007","2","0000000100000009","ERROR");
    CHECK(scalar("SELECT COUNT(*) FROM tool_detections")==before);
    CHECK(scalar("SELECT COUNT(*) FROM loans")==2);
    sql("RENAME TABLE unavailable_actions TO rental_actions");
    detection("E6044006","0000000100000007","2","0000000100000009","RENTED");
    /* Old detections are observations only; upgrading cannot replay them as rentals. */
    sql("INSERT INTO tool_detections(locker_id,camera_id,uid,session_id,tool_label,confidence_milli,event_id,household_id) VALUES('LOCKER_101','CAMERA_LOCKER_101','E6044006','0000000100000008','3',950,'000000010000000A',1)");
    detection("E6044006","0000000100000008","3","000000010000000A","SAVED");
    CHECK(scalar("SELECT rental_state FROM rental_tool_status WHERE tool_no='3'")==0);
    puts("PASS: rent/return, state persistence, event and session dedup, ownership, 5-minute fees, billing, rollback, no historical replay");
    mysql_close(admin); return 0;
}
