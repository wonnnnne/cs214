#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <ctype.h>

#define BUFSIZE 4096

typedef struct {
    int sockfd;
} thread_param_t;

typedef struct _account_t {
    char name[256];
    double balance;
    int in_session;

    struct _account_t* next;
} account_t;

pthread_mutex_t mutex;
account_t* head;

struct itimerval diagnostic_timer;

int need_shutdown = 0;

void assertp(int cond, const char* msg) {
    if(!cond) {
        perror(msg);
        exit(1);
    }
}

// NOTE: 뮤텍스로 보호되어야 한다.
account_t* find_account(const char* name) {
    if(head == NULL) {
        return NULL;
    }

    account_t* p = head;
    while(p) {
        if(strcmp(p->name, name) == 0) {
            return p;
        }

        p = p->next;
    }

    return NULL;
}

int create_new_account(const char* name, char* errmsg) {
    pthread_mutex_lock(&mutex);
    // check exists
    account_t* acc = find_account(name);
    if(acc != NULL) {
        pthread_mutex_unlock(&mutex);
        sprintf(errmsg, "[%s] already exists!", name);
        return 1;
    }

    account_t* new_acc = malloc(sizeof(account_t));
    strcpy(new_acc->name, name);
    new_acc->balance = 0.0f;
    new_acc->in_session = 0;
    new_acc->next = NULL;

    if(head == NULL) {
        head = new_acc;
    } else {
        account_t* p = head;
        while(p->next) {
            p = p->next;
        }

        p->next = new_acc;
    }

    pthread_mutex_unlock(&mutex);
    return 0;
}

// create 명령어
void cmd_create(int sockfd, const char* buff) {
    const char* p = buff + 7;

    // 공백 제거
    while(isspace(*p))
        p++;

    printf("* creating new account: %s\n", p);

    char temp[BUFSIZE] = {0,};
    int ret = create_new_account(p, temp);
    if(ret == 0) {
        sprintf(temp, "account [%s] created!", p);
        write(sockfd, temp, strlen(temp) + 1);
    } else {
        write(sockfd, temp, strlen(temp) + 1);
    }
}

// serve 명령어
int cmd_serve(int sockfd, char* buff) {
    const char* p = buff + 6;

    // 공백 제거
    while(isspace(*p))
        p++;

    printf("* request for service session of account [%s]\n", p);

    char temp[BUFSIZE] = {0,};

    pthread_mutex_lock(&mutex);
    account_t* acc = find_account(p);

    // 해당 계좌가 없을 경우
    if(acc == NULL) {
        pthread_mutex_unlock(&mutex);

        sprintf(temp, "Error: can't find account [%s]", p);
        write(sockfd, temp, strlen(temp) + 1);
        return 1;
    }

    // 다른 클라이언트에 의해 사용되고 있는 계좌일 경우
    if(acc->in_session) {
        pthread_mutex_unlock(&mutex);

        sprintf(temp, "Error: account [%s] is already in use!!", p);
        write(sockfd, temp, strlen(temp) + 1);
        return 1;
    }

    acc->in_session = 1;
    strcpy(buff, acc->name);
    pthread_mutex_unlock(&mutex);

    sprintf(temp, "ok: you're in service session! [%s]", p);
    write(sockfd, temp, strlen(temp) + 1);
    return 0;
}

// deposit 명령어
void cmd_deposit(int sockfd, const char* buff, const char* acc_name) {
    const char* p = buff + 8;

    // 공백 제거
    while(isspace(*p))
        p++;

    printf("* request for deposit [%s]\n", p);

    double val = atof(p);

    pthread_mutex_lock(&mutex);
    account_t* acc = find_account(acc_name);

    acc->balance += val;
    pthread_mutex_unlock(&mutex);

    char temp[BUFSIZE] = {0,};
    sprintf(temp, "ok: success deposit [%f] to account [%s]", val, acc_name);
    write(sockfd, temp, strlen(temp) + 1);
}

// withdraw 명령어
void cmd_withdraw(int sockfd, const char* buff, const char* acc_name) {
    const char* p = buff + 9;

    // 공백 제거
    while(isspace(*p))
        p++;

    printf("* request for withdraw [%s]\n", p);

    double val = atof(p);

    pthread_mutex_lock(&mutex);
    account_t* acc = find_account(acc_name);

    char temp[BUFSIZE] = {0,};

    // 잔액이 부족할 때
    if(acc->balance < val) {
        pthread_mutex_unlock(&mutex);

        sprintf(temp, "Error: balance is insufficient! balance: [%f]", acc->balance);
        write(sockfd, temp, strlen(temp) + 1);
        return;
    }

    acc->balance -= val;
    sprintf(temp, "ok: success withdraw [%f] to account [%s], balance: [%f]", val, acc_name, acc->balance);
    write(sockfd, temp, strlen(temp) + 1);

    pthread_mutex_unlock(&mutex);
}

// query 명령어
void cmd_query(int sockfd, const char* acc_name) {
    pthread_mutex_lock(&mutex);
    account_t* acc = find_account(acc_name);

    char temp[BUFSIZE] = {0,};
    sprintf(temp, "ok: balance of [%s] is [%f] ", acc_name, acc->balance);
    // printf("temp: %s\n", temp);
    write(sockfd, temp, strlen(temp) + 1);

    pthread_mutex_unlock(&mutex);
}

// 서비스 세션 종료
void close_service_session(char* acc_name) {
    pthread_mutex_lock(&mutex);
    account_t* acc = find_account(acc_name);
    acc->in_session = 0;
    acc_name[0] = 0;
    pthread_mutex_unlock(&mutex);
}

// end 명령어
void cmd_end(int sockfd, char* acc_name) {
    close_service_session(acc_name);
    char temp[BUFSIZE] = {0,};
    sprintf(temp, "ok: servie session end!!");
    write(sockfd, temp, strlen(temp) + 1);
}

void send_to_client(int sockfd, const char* msg) {
    write(sockfd, msg, strlen(msg) + 1);
    printf("* send: %s\n", msg);
}

void* client_service_thread_main(void* args) {
    thread_param_t* param = (thread_param_t*)args;

    int sockfd = param->sockfd;

    free(param);

    char buff[BUFSIZE];

    char acc_name_in_service[256] = {0,};
    fd_set readfds;

    while(1) {
        memset(buff, '0', BUFSIZE);

        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int ret = select(sockfd+1, &readfds, NULL, NULL, &tv);
        if(ret <= 0) {
            if(need_shutdown) {
                const char* msg = "now server shutdown!!";
                write(sockfd, msg, strlen(msg) + 1);
                sleep(1);
                write(sockfd, "quit", 5);
                sleep(1);
                printf("@@@ client_service_thread shutdown!!\n");
                break;
            }
            continue;
        }

        long nread = read(sockfd, buff, BUFSIZE);
        if(nread <= 0) {
            printf("* socket closed\n");

            if(strlen(acc_name_in_service) > 0) {
                close_service_session(acc_name_in_service);
            }
            break;
        }

        printf("* recv: %s\n", buff);

        if(strncmp(buff, "quit", 4) == 0) {
            write(sockfd, "quit", 5);
            if(strlen(acc_name_in_service) > 0) {
                close_service_session(acc_name_in_service);
            }
            break;
        }

        if(strncmp(buff, "create ", 7) == 0) {
            if(strlen(acc_name_in_service) > 0) {
                send_to_client(sockfd, "Error: can't create account in service session!");
                continue;
            }
            cmd_create(sockfd, buff);
            continue;
        }

        if(strncmp(buff, "serve ", 6) == 0) {
            if(strlen(acc_name_in_service) > 0) {
                send_to_client(sockfd, "Error: you're already in service session!");
                continue;
            }
            if(cmd_serve(sockfd, buff) == 0)
                strcpy(acc_name_in_service, buff); // accout name copy
            continue;
        }

        if(strncmp(buff, "deposit ", 8) == 0) {
            if(strlen(acc_name_in_service) == 0) {
                send_to_client(sockfd, "Error: deposit command is available in service session!");
                continue;
            }

            cmd_deposit(sockfd, buff, acc_name_in_service);
            continue;
        }

        if(strncmp(buff, "withdraw ", 9) == 0) {
            if(strlen(acc_name_in_service) == 0) {
                send_to_client(sockfd, "Error: withdraw command is available in service session!");
                continue;
            }

            cmd_withdraw(sockfd, buff, acc_name_in_service);
            continue;
        }

        if(strncmp(buff, "query", 5) == 0) {
            if(strlen(acc_name_in_service) == 0) {
                send_to_client(sockfd, "Error: query command is available in service session!");
                continue;
            }

            cmd_query(sockfd, acc_name_in_service);
            continue;
        }

        if(strncmp(buff, "end", 3) == 0) {
            if(strlen(acc_name_in_service) == 0) {
                send_to_client(sockfd, "Error: end command is available in service session!");
                continue;
            }

            cmd_end(sockfd, acc_name_in_service);
            continue;
        }

        send_to_client(sockfd, "Error: unknown command!");
    }

    close(sockfd);
    return 0;
}

void* session_acceptor_thread_main(void* args) {
    int server_sockfd = *((int*)args);

    struct sockaddr_in clientaddr;
    uint client_len = sizeof(clientaddr);
    pthread_t tid;
    int ret;
    fd_set readfds;

    while(1) {
        FD_ZERO(&readfds);
        FD_SET(server_sockfd, &readfds);

        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int ret = select(server_sockfd+1, &readfds, NULL, NULL, &tv);
        if(ret <= 0) {
            if(need_shutdown) {
                const char* msg = "session_acceptor_thread shutdown!!";
                close(server_sockfd);
                return 0;
            }
            continue;
        }

        int client_sockfd = accept(server_sockfd, (struct sockaddr*)&clientaddr, &client_len);

        if(need_shutdown) {
            printf("* session acceptor thread shutdown!\n");
            return 0;
        }

        assertp(client_sockfd != -1, "Accept error: ");

        // create worker thread
        thread_param_t* param = malloc(sizeof(thread_param_t));
        param->sockfd = client_sockfd;
        ret = pthread_create(&tid, NULL, client_service_thread_main, param);
        assertp(ret == 0, "pthread_create: ");

        ret = pthread_detach(tid);
        assertp(ret == 0, "pthread_detach: ");

        printf("* new connection! - worker thread created!![client_sockfd: %d]\n", client_sockfd);
    }
}

void timer_handler(int signum) {
    pthread_mutex_lock(&mutex);
    printf("--------------------\n");
    printf("* Server Diagnostic\n");
    account_t* p = head;
    int count = 0;
    while(p) {
        printf("%s\t%f\t%s\n", p->name, p->balance, p->in_session ? "IN SERVICE" : "");
        p = p->next;
        count++;
    }
    printf("* total: %d\n", count);
    printf("--------------------\n");
    pthread_mutex_unlock(&mutex);
}

void init_global() {
    pthread_mutex_init(&mutex, NULL);
    head = NULL;

    // set diagnostic_timer to be called every 15 secs
    diagnostic_timer.it_value.tv_sec = 15;
    diagnostic_timer.it_value.tv_usec = 0;
    diagnostic_timer.it_interval.tv_sec = 15;
    diagnostic_timer.it_interval.tv_usec = 0;
}

void termination_handler(int dummy) {
    printf("\n* now server shutdown..\n");
    need_shutdown = 1;

    // 타이머 제거
    printf("* remove timer.\n");
    diagnostic_timer.it_value.tv_sec = 0;
    diagnostic_timer.it_interval.tv_sec = 0;
    setitimer(ITIMER_REAL, &diagnostic_timer, NULL);
}

int main(int argc, char* argv[]) {
    if(argc != 2) {
        printf("* Usage: ./bankingServer PORT\n");
        return 0;
    }

    init_global();

    // ctrl + c 핸들러 등록
    signal(SIGINT, termination_handler);

    int port = atoi(argv[1]);
    int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    assertp(server_sockfd >= 0, "create socket error : ");

    struct sockaddr_in serveraddr;
    bzero(&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(port);

    int ret;
    ret = bind(server_sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
    assertp(ret != -1, "bind error: ");

    ret = listen(server_sockfd, 5);
    assertp(ret != -1, "listen error: ");

    printf("* server started! port[%d]\n", port);

    // create session acceptor thread
    pthread_t tid;
    ret = pthread_create(&tid, NULL, session_acceptor_thread_main, &server_sockfd);
    assertp(ret == 0, "pthread_create: ");
    printf("* session acceptor thread created!\n");
    printf("* now listening...\n");

    // start diagnostic_timer
    struct sigaction sa;

    // Install timer_handler as the signal handler for SIGALARM
    memset (&sa, 0, sizeof(sa));
    sa.sa_handler = &timer_handler;
    sigaction(SIGALRM, &sa, NULL);
    setitimer(ITIMER_REAL, &diagnostic_timer, NULL);

    pthread_join(tid, NULL);

    // service thread 가 모두 끝날때까지 잠시 대기 
    sleep(5);

    printf("* destroy mutex.\n");
    pthread_mutex_lock(&mutex);
    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);

    // dealloc memory
    printf("* dealloc memory.\n");
    account_t* p = head;
    while(p) {
        account_t* temp = p;
        p = p->next;
        free(temp);
    }

    printf("* bye~\n");
    return 0;
}
