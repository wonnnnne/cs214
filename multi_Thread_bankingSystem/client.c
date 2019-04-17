#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <netdb.h>

#define BUFSIZE 4096

int server_shutdown = 0;

typedef struct {
    int sockfd;
} thread_param_t;

void assertp(int cond, const char* msg) {
    if(!cond) {
        perror(msg);
        exit(1);
    }
}

void print_invalid_command(const char* s) {
    printf("invalid command: %s\n", s);
}

// create 명령어
void cmd_create(int sockfd, const char* buff) {
    const char* p = buff + 7;

    while(isspace(*p))
        p++;

    if(strlen(p) < 3) {
        print_invalid_command(buff);
        return;
    }

    printf("* send: [%s]\n", buff);
    int nwrite = write(sockfd, buff, strlen(buff) + 1);
    printf("* nwrite: [%d]\n", nwrite);
}

// serve 명령어
void cmd_serve(int sockfd, const char* buff) {
    const char* p = buff + 6;

    while(isspace(*p))
        p++;

    if(strlen(p) < 3) {
        print_invalid_command(buff);
        return;
    }

    printf("* send: [%s]\n", buff);
    write(sockfd, buff, strlen(buff) + 1);
}

// deposit 명령어
void cmd_deposit(int sockfd, const char* buff) {
    const char* p = buff + 8;

    while(isspace(*p))
        p++;

    double val = atof(p);
    if(val <= 0) {
        print_invalid_command(buff);
        return;
    }

    printf("* send: [%s]\n", buff);
    write(sockfd, buff, strlen(buff) + 1);
}

// withdraw 명령어
void cmd_withdraw(int sockfd, const char* buff) {
    const char* p = buff + 9;

    while(isspace(*p))
        p++;

    double val = atof(p);
    if(val <= 0) {
        print_invalid_command(buff);
        return;
    }

    printf("* send: [%s]\n", buff);
    write(sockfd, buff, strlen(buff) + 1);
}

// query 명령어
void cmd_query(int sockfd) {
    printf("* send: [query]\n");
    write(sockfd, "query", 6);
}

// end 명령어
void cmd_end(int sockfd) {
    printf("* send: [end]\n");
    write(sockfd, "end", 4);
}

// quit 명령어
void cmd_quit(int sockfd) {
    printf("* send: [quit]\n");
    write(sockfd, "quit", 5);
}

// 명령어 처리 스레드 메인 함수
void* command_thread_main(void* args) {
    thread_param_t* param = (thread_param_t*)args;
    int sockfd = param->sockfd;

    char buff[BUFSIZE];

    fd_set readfds;

    while(1) {
        // 명령어 처리 마다 2초 딜레이를 준다.
        sleep(2);
        printf("> input command: ");
        fflush(NULL);

        while(1) {
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);

            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int ret = select(STDIN_FILENO+1, &readfds, NULL, NULL, &tv);
            if(ret <= 0) {
                if(server_shutdown) {
                    return 0;
                }
                continue;
            }
            else {
                break;
            }
        }

        fgets(buff, BUFSIZE, stdin);

        // remove new line charactor
        buff[strlen(buff) - 1] = 0;

        if(strncmp(buff, "create ", 7) == 0) {
            cmd_create(sockfd, buff);
            continue;
        }

        if(strncmp(buff, "serve ", 6) == 0) {
            cmd_serve(sockfd, buff);
            continue;
        }

        if(strncmp(buff, "deposit ", 8) == 0) {
            cmd_deposit(sockfd, buff);
            continue;
        }

        if(strncmp(buff, "withdraw ", 9) == 0) {
            cmd_withdraw(sockfd, buff);
            continue;
        }

        if(strncmp(buff, "query", 5) == 0) {
            cmd_query(sockfd);
            continue;
        }

        if(strncmp(buff, "end", 3) == 0) {
            cmd_end(sockfd);
            continue;
        }

        if(strncmp(buff, "quit", 4) == 0) {
            cmd_quit(sockfd);
            close(sockfd);
            printf("* quit!\n");
            return 0;
        }

        print_invalid_command(buff);
    }
}

// 응답 처리 스레드 메인 함수
void* response_thread_main(void* args) {
    thread_param_t* param = (thread_param_t*)args;
    int sockfd = param->sockfd;

    char buff[BUFSIZE];

    while(1) {
        long nread = read(sockfd, buff, BUFSIZE);
        if(nread <= 0) {
            printf("* socket closed\n");
            return 0;
        }

        if(strncmp(buff, "quit", 4) == 0) {
            close(sockfd);
            printf("* quit!\n");
            server_shutdown = 1;
            return 0;
        }

        printf("* recv: %s\n", buff);
    }
}

int main(int argc, char* argv[]) {
    if(argc != 3) {
        printf("* Usage: ./bankingClient SERVER_ADDR PORT\n");
        printf("     ex: ./bankingClient localhost 8888\n");
        return 0;
    }

    char ip_str[256] = {0,};

    // ip 주소일 경우
    if(isdigit(argv[1][0])) {
        strcpy(ip_str, argv[1]);
    }
    else { // 도메인 일경우
        struct hostent *host = gethostbyname(argv[1]);
        assertp(host > 0, "gethostbyname: ");

        printf("* Host name : %s\n", host->h_name);
        strcpy(ip_str, inet_ntoa(*(struct in_addr*)host->h_addr_list[0]));
        printf("* IP address: %s\n", ip_str);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip_str);
    addr.sin_port = htons(atoi(argv[2]));

    int ret;
    printf("* now connecting to server...\n");
    ret = connect(sockfd, (struct sockaddr*)&addr, sizeof(addr));
    assertp(ret >= 0, "connect: ");

    thread_param_t* param = malloc(sizeof(thread_param_t));
    param->sockfd = sockfd;

    // create response thread
    pthread_t tid1;
    ret = pthread_create(&tid1, NULL, response_thread_main, param);
    assertp(ret == 0, "pthread_create: ");
    printf("* response thread created!\n");

    // create command thread
    pthread_t tid2;
    ret = pthread_create(&tid2, NULL, command_thread_main, param);
    assertp(ret == 0, "pthread_create: ");
    printf("* command thread created!\n");

    // 두 스레드가 끝날때까지 대기
    pthread_join(tid2, NULL);
    pthread_join(tid1, NULL);

    free(param);
}
