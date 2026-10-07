/* Run only against the isolated fixture DB used by run_rental_tests.sh. */
#define main admin_server_program_main
#include "admin_server.c"
#undef main
#include <sys/wait.h>
#include <time.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static pid_t server_pid=-1;
static void cleanup(void) { if(server_pid>0) { kill(server_pid,SIGTERM); waitpid(server_pid,NULL,0); } }
static void sql(MYSQL *db,const char *query) { if(mysql_query(db,query)) { fprintf(stderr,"Fixture: %s\n",mysql_error(db)); exit(1); } }
static long scalar(MYSQL *db,const char *query) {
    sql(db,query); MYSQL_RES *res=mysql_store_result(db); CHECK(res); MYSQL_ROW row=mysql_fetch_row(res);
    CHECK(row&&row[0]); long n=strtol(row[0],NULL,10); mysql_free_result(res); return n;
}
static int connect_client(unsigned port) {
    int fd=socket(AF_INET,SOCK_STREAM,0); CHECK(fd>=0);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons((unsigned short)port),.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
    if(connect(fd,(struct sockaddr*)&address,sizeof(address))) { close(fd); return -1; }
    struct timeval timeout={.tv_sec=5}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)); return fd;
}
static buffer_t request(unsigned port,const char *method,const char *path,int status) {
    int fd=connect_client(port); CHECK(fd>=0); char line[1024];
    int n=snprintf(line,sizeof(line),"%s %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",method,path);
    /* Fragment the request to verify TCP framing. */
    CHECK(!send_all(fd,line,7)); CHECK(!send_all(fd,line+7,(size_t)n-7));
    buffer_t response={0}; char bytes[4096]; ssize_t got;
    while((got=recv(fd,bytes,sizeof(bytes),0))>0) append(&response,bytes,(size_t)got);
    CHECK(got==0 && !response.failed); close(fd);
    char prefix[32]; snprintf(prefix,sizeof(prefix),"HTTP/1.1 %d ",status); CHECK(!strncmp(response.data,prefix,strlen(prefix)));
    CHECK(strstr(response.data,"Cache-Control: no-store\r\n"));
    CHECK(strstr(response.data,"Content-Security-Policy:"));
    char *body=strstr(response.data,"\r\n\r\n"); CHECK(body); body+=4;
    char *length=strstr(response.data,"Content-Length: "); CHECK(length);
    CHECK(strtoul(length+16,NULL,10)==strlen(body));
    buffer_t result={0}; append(&result,body,strlen(body)); free(response.data); return result;
}
static const char *json_cursor;
static void whitespace(void) { while(*json_cursor==' '||*json_cursor=='\n'||*json_cursor=='\r'||*json_cursor=='\t') json_cursor++; }
static void json_value(void);
static void json_text(void) {
    CHECK(*json_cursor++=='"');
    while(*json_cursor && *json_cursor!='"') {
        CHECK((unsigned char)*json_cursor>=32);
        if(*json_cursor++=='\\') {
            char escaped=*json_cursor++; CHECK(escaped);
            if(escaped=='u') { for(int i=0;i<4;++i) { CHECK(*json_cursor && strchr("0123456789abcdefABCDEF",*json_cursor)); json_cursor++; } }
            else CHECK(strchr("\"\\/bfnrt",escaped));
        }
    }
    CHECK(*json_cursor++=='"');
}
static void json_value(void) {
    whitespace();
    if(*json_cursor=='"') { json_text(); return; }
    if(*json_cursor=='{' || *json_cursor=='[') {
        int object=*json_cursor++=='{'; char end=object?'}':']'; whitespace();
        if(*json_cursor==end) { json_cursor++; return; }
        for(;;) {
            if(object) { json_text(); whitespace(); CHECK(*json_cursor++==':'); }
            json_value(); whitespace(); if(*json_cursor==end) { json_cursor++; return; }
            CHECK(*json_cursor++==','); whitespace();
        }
    }
    if(!strncmp(json_cursor,"null",4)) { json_cursor+=4; return; }
    if(*json_cursor=='-') json_cursor++;
    CHECK(*json_cursor>='0'&&*json_cursor<='9');
    if(*json_cursor=='0') json_cursor++;
    else while(*json_cursor>='0'&&*json_cursor<='9') json_cursor++;
    if(*json_cursor=='.') { json_cursor++; CHECK(*json_cursor>='0'&&*json_cursor<='9'); while(*json_cursor>='0'&&*json_cursor<='9') json_cursor++; }
}
static unsigned occurrences(const char *text,const char *needle) {
    unsigned n=0; for(const char *p=text;(p=strstr(p,needle));p+=strlen(needle)) ++n; return n;
}
static void check_json(const char *text) { json_cursor=text; json_value(); whitespace(); CHECK(!*json_cursor); }
int main(int argc,char **argv) {
    CHECK(argc==2 && getenv("DB_SOCKET")); CHECK(!atexit(cleanup));
    MYSQL *db=mysql_init(NULL); CHECK(db);
    CHECK(mysql_real_connect(db,"localhost","root","","parts_rental",0,getenv("DB_SOCKET"),0));
    CHECK(!mysql_set_character_set(db,"utf8mb4"));
    CHECK(!setenv("DB_PASSWORD","temporary-test-password",1));
    filter_t empty_filter={1,0,0,0}; buffer_t empty={0}; CHECK(!dashboard(&empty,&empty_filter));
    check_json(empty.data); CHECK(strstr(empty.data,"\"households\":[]")); CHECK(strstr(empty.data,"\"loans\":[]"));
    free(empty.data);
    sql(db,"INSERT INTO households(id,building_no,unit_no) VALUES(3,'102','101')");
    sql(db,"UPDATE rental_tools SET tool_name='<script>\"공구\\\\이름' WHERE id=5");
    for(int i=0;i<25;++i) sql(db,"INSERT INTO loans(household_id,tool_id,rental_date,due_date,return_date,rental_fee_won,daily_late_fee_won,late_fee_period_seconds) VALUES(3,5,UTC_TIMESTAMP(6)+INTERVAL 8 HOUR,UTC_TIMESTAMP(6)+INTERVAL 8 HOUR+INTERVAL 5 MINUTE,UTC_TIMESTAMP(6)+INTERVAL 8 HOUR+INTERVAL 6 MINUTE,500,500,300)");
    sql(db,"INSERT INTO loans(household_id,tool_id,rental_date,due_date,rental_fee_won,daily_late_fee_won,late_fee_period_seconds) VALUES(3,5,UTC_TIMESTAMP(6)+INTERVAL 9 HOUR-INTERVAL 6 MINUTE,UTC_TIMESTAMP(6)+INTERVAL 9 HOUR-INTERVAL 1 MINUTE,500,500,300)");
    long before=scalar(db,"SELECT COUNT(*) FROM loans");
    CHECK(!setenv("DB_PASSWORD","temporary-test-password",1));
    filter_t filter={1,0,0,0}; buffer_t direct={0}; CHECK(!dashboard(&direct,&filter)); check_json(direct.data); free(direct.data);
    int probe=socket(AF_INET,SOCK_STREAM,0); CHECK(probe>=0);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
    CHECK(!bind(probe,(struct sockaddr*)&address,sizeof(address))); socklen_t length=sizeof(address);
    CHECK(!getsockname(probe,(struct sockaddr*)&address,&length)); unsigned port=ntohs(address.sin_port); close(probe);
    char port_text[16]; snprintf(port_text,sizeof(port_text),"%u",port);
    server_pid=fork(); CHECK(server_pid>=0);
    if(!server_pid) { execl(argv[1],argv[1],"--port",port_text,"--bind","127.0.0.1","--web-root","admin",(char*)NULL); _exit(127); }
    int ready=-1;
    for(int i=0;i<50 && ready<0;++i) { ready=connect_client(port); if(ready<0) { struct timespec pause={.tv_nsec=20000000}; nanosleep(&pause,NULL); } }
    CHECK(ready>=0); close(ready);
    buffer_t response=request(port,"GET","/",200); CHECK(strstr(response.data,"세대별 현황")); free(response.data);
    response=request(port,"GET","/dashboard.css",200); CHECK(strstr(response.data,"@media")); free(response.data);
    response=request(port,"GET","/dashboard.js",200); CHECK(strstr(response.data,"textContent")); free(response.data);
    response=request(port,"GET","/api/dashboard",200); check_json(response.data);
    CHECK(strstr(response.data,"\"duration_seconds\":300")); CHECK(strstr(response.data,"\"late_fee_period_seconds\":300"));
    CHECK(strstr(response.data,"\"rental_fee_won\":500")); CHECK(!strstr(response.data,"<script>"));
    CHECK(strstr(response.data,"\\u003cscript\\u003e")); CHECK(strstr(response.data,"\"return_date\":null"));
    FILE *fixture=fopen("build/wifi-check/admin-test-dashboard.json","wb"); CHECK(fixture);
    CHECK(fwrite(response.data,1,response.used,fixture)==response.used); CHECK(!fclose(fixture)); free(response.data);
    response=request(port,"GET","/api/dashboard?household=3&tool=5&state=2&page=2",200); check_json(response.data);
    CHECK(strstr(response.data,"\"total\":25"));
    const char *loans=strstr(response.data,"\"loans\":"); CHECK(loans);
    CHECK(occurrences(loans,"\"loan_id\":")==5); free(response.data);
    response=request(port,"GET","/api/dashboard?household=3&state=3",200); check_json(response.data); CHECK(strstr(response.data,"\"total\":1")); free(response.data);
    response=request(port,"GET","/api/dashboard?household=999",200); check_json(response.data); CHECK(strstr(response.data,"\"loans\":[]")); free(response.data);
    response=request(port,"GET","/api/dashboard?page=0",400); free(response.data);
    response=request(port,"GET","/api/dashboard?household=1%20OR%201",400); free(response.data);
    response=request(port,"POST","/api/dashboard",405); free(response.data);
    response=request(port,"GET","/../tools/sql_client.c",404); free(response.data);
    response=request(port,"GET","/rental_schema.sql",404); free(response.data);
    sql(db,"RENAME TABLE rental_policy TO unavailable_policy");
    response=request(port,"GET","/api/dashboard",503); check_json(response.data); CHECK(strstr(response.data,"DB 조회 실패")); free(response.data);
    sql(db,"RENAME TABLE unavailable_policy TO rental_policy");
    CHECK(scalar(db,"SELECT COUNT(*) FROM loans")==before);
    CHECK(scalar(db,"SELECT duration_seconds FROM rental_policy WHERE id=1")==300);
    puts("PASS: read-only live DB HTTP dashboard, Unicode/JSON escaping, pages/filters, empty state, policy preservation, failure handling, route isolation");
    mysql_close(db); return 0;
}
