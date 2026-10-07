/* Read-only LAN dashboard. No dependencies other than the existing MariaDB C client. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <mysql.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define BODY_LIMIT (4U*1024U*1024U)
#define PAGE_SIZE 20
typedef struct { char *data; size_t used,capacity; int failed; } buffer_t;
typedef struct { unsigned page,household,tool,state; } filter_t;
static char last_db_error[384];

static int reserve(buffer_t *b,size_t extra)
{
    if (b->failed || extra>BODY_LIMIT-b->used) { b->failed=1; return -1; }
    size_t needed=b->used+extra+1;
    if(needed>b->capacity) {
        size_t capacity=b->capacity ? b->capacity : 4096;
        while(capacity<needed) capacity*=2;
        char *data=realloc(b->data,capacity);
        if(!data) { b->failed=1; return -1; }
        b->data=data; b->capacity=capacity;
    }
    return 0;
}
static void append(buffer_t *b,const char *text,size_t n)
{
    if(reserve(b,n)) return;
    memcpy(b->data+b->used,text,n); b->used+=n; b->data[b->used]=0;
}
static void print(buffer_t *b,const char *format,...)
{
    va_list args,copy; va_start(args,format); va_copy(copy,args);
    int n=vsnprintf(NULL,0,format,copy); va_end(copy);
    if(n<0 || reserve(b,(size_t)n)) { b->failed=1; va_end(args); return; }
    vsnprintf(b->data+b->used,b->capacity-b->used,format,args); va_end(args); b->used+=(size_t)n;
}
static void json_string(buffer_t *b,const char *value,size_t n)
{
    append(b,"\"",1);
    for(size_t i=0;i<n;++i) {
        unsigned char ch=(unsigned char)value[i];
        if(ch=='"' || ch=='\\') { char escaped[2]={'\\',(char)ch}; append(b,escaped,2); }
        else if(ch<32 || ch=='<' || ch=='>' || ch=='&') print(b,"\\u%04x",ch);
        else append(b,(const char*)&value[i],1);
    }
    append(b,"\"",1);
}
static const char *env(const char *key,const char *fallback)
{
    const char *value=getenv(key); return value&&*value ? value : fallback;
}
static void db_error(MYSQL *db)
{
    unsigned code=mysql_errno(db);
    const char *hint=code==1045 ? "DB_USER와 DB_PASSWORD를 확인하세요." :
        code==1142 ? "DB 계정의 조회 권한을 확인하세요." :
        (code==1146 || code==1356 || code==1054) ? "대여 DB 테이블·뷰와 조회 권한을 확인하세요." :
        (code==2002 || code==2003) ? "MariaDB 실행 상태와 DB_HOST를 확인하세요." :
        "관리자 서버 터미널의 오류 내용을 확인하세요.";
    snprintf(last_db_error,sizeof(last_db_error),"DB 조회 실패 (%u): %s",code,hint);
    fprintf(stderr,"Dashboard DB error %u: %s\n",code,mysql_error(db));
}
static MYSQL *connect_db(void)
{
    if(!getenv("DB_PASSWORD") || !*getenv("DB_PASSWORD")) return NULL;
    MYSQL *db=mysql_init(NULL); if(!db) return NULL;
    unsigned timeout=3;
    if(mysql_options(db,MYSQL_OPT_CONNECT_TIMEOUT,&timeout) ||
       mysql_options(db,MYSQL_OPT_READ_TIMEOUT,&timeout) ||
       mysql_options(db,MYSQL_OPT_WRITE_TIMEOUT,&timeout) ||
       !mysql_real_connect(db,env("DB_HOST","localhost"),env("DB_USER","parts_app"),
                           getenv("DB_PASSWORD"),env("DB_NAME","parts_rental"),0,getenv("DB_SOCKET"),0) ||
       mysql_set_character_set(db,"utf8mb4")) {
        db_error(db);
        mysql_close(db); return NULL;
    }
    return db;
}
static int numeric_field(enum enum_field_types type)
{
    return type==MYSQL_TYPE_TINY || type==MYSQL_TYPE_SHORT || type==MYSQL_TYPE_LONG ||
           type==MYSQL_TYPE_LONGLONG || type==MYSQL_TYPE_INT24 || type==MYSQL_TYPE_DECIMAL ||
           type==MYSQL_TYPE_NEWDECIMAL;
}
static int query_json(MYSQL *db,buffer_t *b,const char *query)
{
    if(mysql_query(db,query)) {
        db_error(db); return -1;
    }
    MYSQL_RES *res=mysql_store_result(db); if(!res) { db_error(db); return -1; }
    unsigned count=mysql_num_fields(res); MYSQL_FIELD *fields=mysql_fetch_fields(res);
    append(b,"[",1); MYSQL_ROW row; unsigned rows=0;
    while((row=mysql_fetch_row(res))) {
        unsigned long *lengths=mysql_fetch_lengths(res);
        if(rows++) append(b,",",1);
        append(b,"{",1);
        for(unsigned i=0;i<count;++i) {
            if(i) append(b,",",1);
            json_string(b,fields[i].name,strlen(fields[i].name)); append(b,":",1);
            if(!row[i]) append(b,"null",4);
            else if(numeric_field(fields[i].type)) append(b,row[i],lengths[i]);
            else json_string(b,row[i],lengths[i]);
        }
        append(b,"}",1);
    }
    append(b,"]",1); mysql_free_result(res); return b->failed ? -1 : 0;
}
static int parse_uint(const char *value,unsigned max,unsigned *out)
{
    if(!*value) return -1;
    unsigned long number=0;
    for(const char *p=value;*p;++p) {
        if(*p<'0'||*p>'9') return -1;
        number=number*10U+(unsigned)(*p-'0'); if(number>max) return -1;
    }
    *out=(unsigned)number; return 0;
}
static int filters(char *query,filter_t *f)
{
    *f=(filter_t){1,0,0,0}; if(!query || !*query) return 0;
    char *save=NULL;
    for(char *part=strtok_r(query,"&",&save);part;part=strtok_r(NULL,"&",&save)) {
        char *equal=strchr(part,'='); if(!equal) return -1; *equal++=0;
        unsigned *target,max;
        if(!strcmp(part,"page")) { target=&f->page; max=1000000; }
        else if(!strcmp(part,"household")) { target=&f->household; max=2147483647; }
        else if(!strcmp(part,"tool")) { target=&f->tool; max=2147483647; }
        else if(!strcmp(part,"state")) { target=&f->state; max=3; }
        else return -1;
        if(parse_uint(equal,max,target)) return -1;
    }
    return f->page ? 0 : -1;
}
static int dashboard(buffer_t *body,const filter_t *f)
{
    strcpy(last_db_error,"DB 조회 실패. DB 연결 설정과 서버 터미널을 확인하세요.");
    MYSQL *db=connect_db(); if(!db) return -1;
    int result=-1;
    if(mysql_query(db,"SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ") ||
       mysql_query(db,"START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY")) { db_error(db); goto done; }
    append(body,"{\"clock\":",9);
    if(query_json(db,body,"SELECT DATE_FORMAT(UTC_TIMESTAMP(6)+INTERVAL 9 HOUR,'%Y-%m-%dT%H:%i:%s') AS now")) goto done;
    append(body,",\"policy\":",10);
    if(query_json(db,body,"SELECT duration_seconds,rental_fee_won,late_fee_won,late_fee_period_seconds FROM rental_policy WHERE id=1")) goto done;
    append(body,",\"households\":",14);
    if(query_json(db,body,"SELECT * FROM household_rental_summary ORDER BY building_no,unit_no,household_id")) goto done;
    append(body,",\"tools\":",9);
    if(query_json(db,body,"SELECT t.*,h.building_no,h.unit_no,COALESCE(l.overdue_periods,0) AS overdue_periods FROM rental_tool_status t LEFT JOIN households h ON h.id=t.household_id LEFT JOIN loan_list l ON l.loan_id=t.loan_id ORDER BY t.id")) goto done;
    char where[512],query[2048];
    snprintf(where,sizeof(where),"WHERE (%u=0 OR household_id=%u) AND (%u=0 OR tool_no=(SELECT tool_no FROM rental_tools WHERE id=%u)) %s",
             f->household,f->household,f->tool,f->tool,
             f->state==1 ? "AND is_returned=0" : f->state==2 ? "AND is_returned=1" :
             f->state==3 ? "AND is_returned=0 AND overdue_periods>0" : "");
    snprintf(query,sizeof(query),"SELECT COUNT(*) AS total FROM loan_list %s",where);
    append(body,",\"count\":",9); if(query_json(db,body,query)) goto done;
    snprintf(query,sizeof(query),"SELECT * FROM loan_list %s ORDER BY loan_id DESC LIMIT %d OFFSET %u",where,PAGE_SIZE,(f->page-1U)*PAGE_SIZE);
    append(body,",\"loans\":",9); if(query_json(db,body,query)) goto done;
    print(body,",\"page\":%u,\"page_size\":%d}",f->page,PAGE_SIZE);
    if(body->failed || mysql_commit(db)) goto done;
    result=0;
done:
    if(result) mysql_rollback(db);
    mysql_close(db); return result;
}
static int send_all(int fd,const char *data,size_t n)
{
    while(n) {
        ssize_t written=send(fd,data,n,0);
        if(written<0 && errno==EINTR) continue;
        if(written<=0) return -1;
        data+=written; n-=(size_t)written;
    }
    return 0;
}
static void respond(int fd,int status,const char *type,const char *body,size_t n)
{
    char header[1024];
    const char *reason=status==200 ? "OK" : status==400 ? "Bad Request" : status==404 ? "Not Found" :
                       status==405 ? "Method Not Allowed" : "Service Unavailable";
    int length=snprintf(header,sizeof(header),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; "
        "connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'\r\n\r\n",status,reason,type,n);
    if(length>0 && (size_t)length<sizeof(header) && !send_all(fd,header,(size_t)length)) send_all(fd,body,n);
}
static void message(int fd,int status,const char *text)
{
    respond(fd,status,"application/json; charset=utf-8",text,strlen(text));
}
static void serve_file(int fd,const char *root,const char *file,const char *type)
{
    char path[4096];
    int n=snprintf(path,sizeof(path),"%s/%s",root,file);
    if(n<0 || (size_t)n>=sizeof(path)) { message(fd,404,"{\"error\":\"화면 파일을 찾을 수 없습니다\"}"); return; }
    FILE *input=fopen(path,"rb");
    if(!input) { message(fd,404,"{\"error\":\"화면 파일을 찾을 수 없습니다\"}"); return; }
    buffer_t body={0}; char block[8192]; size_t got;
    while((got=fread(block,1,sizeof(block),input))) { append(&body,block,got); if(body.failed) break; }
    int error=ferror(input)||body.failed; fclose(input);
    if(error) message(fd,503,"{\"error\":\"화면 파일 읽기 실패\"}");
    else respond(fd,200,type,body.data?body.data:"",body.used);
    free(body.data);
}
static int check_assets(const char *root)
{
    const char *files[]={"index.html","dashboard.css","dashboard.js"};
    for(unsigned i=0;i<3;++i) {
        char path[4096];
        int n=snprintf(path,sizeof(path),"%s/%s",root,files[i]);
        if(n<0 || (size_t)n>=sizeof(path)) return -1;
        FILE *file=fopen(path,"rb");
        if(!file) {
            fprintf(stderr,"Missing dashboard file: %s\nCopy the complete admin folder, or correct --web-root.\n",path);
            return -1;
        }
        fclose(file);
    }
    return 0;
}
static void handle_client(int fd,const char *root)
{
    char request[8192]; size_t used=0;
    while(used<sizeof(request)-1U) {
        ssize_t n=recv(fd,request+used,sizeof(request)-1U-used,0);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) return;
        used+=(size_t)n; request[used]=0;
        if(memchr(request,0,used)) { message(fd,400,"{\"error\":\"Invalid request\"}"); return; }
        if(strstr(request,"\r\n\r\n")) break;
    }
    if(!strstr(request,"\r\n\r\n")) { message(fd,400,"{\"error\":\"Headers too large\"}"); return; }
    char *line=strstr(request,"\r\n"); if(!line) return; *line=0;
    char *path=strchr(request,' '); if(!path) { message(fd,400,"{}"); return; } *path++=0;
    char *version=strchr(path,' '); if(!version) { message(fd,400,"{}"); return; } *version++=0;
    if(strcmp(version,"HTTP/1.1") && strcmp(version,"HTTP/1.0")) { message(fd,400,"{}"); return; }
    if(strcmp(request,"GET")) { message(fd,405,"{\"error\":\"조회만 가능합니다\"}"); return; }
    char *query=strchr(path,'?'); if(query) *query++=0;
    if(!strcmp(path,"/api/dashboard")) {
        filter_t f;
        if(filters(query,&f)) { message(fd,400,"{\"error\":\"잘못된 조회 조건입니다\"}"); return; }
        buffer_t body={0};
        if(dashboard(&body,&f)) {
            buffer_t error={0}; append(&error,"{\"error\":",9);
            json_string(&error,last_db_error,strlen(last_db_error)); append(&error,"}",1);
            if(error.failed) message(fd,503,"{\"error\":\"DB 조회 실패\"}");
            else respond(fd,503,"application/json; charset=utf-8",error.data,error.used);
            free(error.data);
        } else respond(fd,200,"application/json; charset=utf-8",body.data,body.used);
        free(body.data);
    } else if(!strcmp(path,"/") || !strcmp(path,"/index.html")) serve_file(fd,root,"index.html","text/html; charset=utf-8");
    else if(!strcmp(path,"/dashboard.css")) serve_file(fd,root,"dashboard.css","text/css; charset=utf-8");
    else if(!strcmp(path,"/dashboard.js")) serve_file(fd,root,"dashboard.js","text/javascript; charset=utf-8");
    else message(fd,404,"{\"error\":\"페이지를 찾을 수 없습니다\"}");
}
int main(int argc,char **argv)
{
    unsigned port=8080; const char *root="admin",*bind_ip="0.0.0.0";
    for(int i=1;i<argc;i+=2) {
        if(i+1>=argc) goto usage;
        if(!strcmp(argv[i],"--port")) { if(parse_uint(argv[i+1],65535,&port)||!port) goto usage; }
        else if(!strcmp(argv[i],"--web-root")) root=argv[i+1];
        else if(!strcmp(argv[i],"--bind")) bind_ip=argv[i+1];
        else goto usage;
    }
    if(!getenv("DB_PASSWORD") || !*getenv("DB_PASSWORD")) {
        fputs("Set DB_PASSWORD before starting admin_server\n",stderr); return 1;
    }
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons((unsigned short)port)};
    if(inet_pton(AF_INET,bind_ip,&address.sin_addr)!=1) goto usage;
    if(check_assets(root)) return 1;
    signal(SIGPIPE,SIG_IGN); setvbuf(stdout,NULL,_IOLBF,0);
    int listener=socket(AF_INET,SOCK_STREAM,0); if(listener<0) { perror("socket"); return 1; }
    int reuse=1; setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    if(bind(listener,(struct sockaddr*)&address,sizeof(address))||listen(listener,16)) {
        perror("bind/listen"); close(listener); return 1;
    }
    printf("Admin dashboard C - read only; http://%s:%u/ (web root: %s)\n",bind_ip,port,root);
    for(;;) {
        int client=accept(listener,NULL,NULL);
        if(client<0) { if(errno==EINTR) continue; perror("accept"); break; }
        struct timeval timeout={.tv_sec=3};
        setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        handle_client(client,root); close(client);
    }
    close(listener); return 1;
usage:
    fprintf(stderr,"Usage: %s [--port 8080] [--web-root admin] [--bind 0.0.0.0]\n",argv[0]); return 1;
}
