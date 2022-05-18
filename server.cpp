/*
** server.c -- a stream socket server demo
*/
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/mman.h>
#include "map"
#include <iostream>
#include <iterator>


#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10   // how many pending connections queue will hold


/////////  MALLOC //////////////

std::map<void*, size_t> allocated_pointers;

void* my_malloc(size_t size) {
    void* ptr= mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!ptr) return NULL;
    allocated_pointers[ptr]= size + 1;
    return ptr;
}

void my_free(void* ptr){
    munmap(ptr, allocated_pointers.at(ptr));
    allocated_pointers.erase(ptr);
}

///////////////// STACK //////////////

typedef struct node{
    char *value;
    struct node* next;
}Node;

typedef struct stack{
    Node* Stack_head;
    size_t size;
}Stack;

static Stack* st;


Stack* createStack(){
    st= (Stack*) my_malloc(sizeof(Stack));
    st->Stack_head= nullptr;
    st->size=0;
    return st;
}


void push(Stack* st, char* str){
    Node* n = (Node*) my_malloc(sizeof(Node));
    if(!n) return;
    n->value= (char*) my_malloc(1024);
    strcpy(n->value,str);

    n->next= st->Stack_head;
    st->Stack_head= n;
    st->size++;

}

void pop(Stack* st){
    Node* n = st->Stack_head;
    if(st->size>0){
        st->Stack_head=n->next;
        my_free(n);
        st->size--;
    }
    else puts("ERROR: <there nothing in this stack [POP]>");
}

char* top(Stack* st, char* str){
    if (!str) return NULL;

    if (st->size>0){
        strcpy(str,"OUTPUT: ");
        strcat(str,st->Stack_head->value);
        return str;
    }
    strcpy(str,"ERROR: <there nothing in this stack [TOP]>");
    return str;
}

void printStack(Stack* st){
    Node* n = st->Stack_head;
    while (st->size>0 && n!=NULL) {
        printf("DEBUG: %s\n",n->value);
        n=n->next;
    }
}

void freeStack(Stack* st){
    while (st->size>0) pop(st);
    free(st);
}

//////// SERVER ///////////////

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


void remove_first_n_chars(char* str, int n){
    int k=0;
    for (size_t i = n; i < strlen(str)+1; i++)
        str[k++]=str[i];
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int server()
{
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    char txt[1024];
    char buf[1024];
    st=createStack();

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr *)&their_addr),
                  s, sizeof s);
        printf("server: got connection from %s\n", s);

        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener

            while(1) {
                strcpy(txt, "");
                strcpy(buf, "");

                if(recv(new_fd, txt, 1024, 0)<0)
                    perror("server: recv");

                if (strncmp(txt, "PUSH ", 5) == 0) {
                    remove_first_n_chars(txt, 5);
                    push(st, txt);
                } else if (strncmp(txt, "POP", 3) == 0) {
                    pop(st);
                } else if (strncmp(txt, "TOP", 3) == 0) {
                    char s[1024]="";
                    top(st,s);
                    strcpy(buf, s);
                    strcat(buf, "\n");

                }
                else if (strncmp(txt, "prints", 6) == 0) printStack(st);

                if (send(new_fd, buf, strlen(buf) + 1, 0) == -1)
                    perror("send");

            }
            freeStack(st);
            close(new_fd);
            exit(0);

        }

    }

    return 0;
}

int Test(){
    char *s = (char*) my_malloc(1024);
    st=createStack();

    assert(st!=NULL);
    push(st,"a1");
    push(st,"a2");
    push(st,"a3");


    top(st,s);
    if (s) {
        assert(strcmp(s,"a3"));
    }
    pop(st);

    top(st,s);
    if (s) {
        assert(strcmp(s,"a2"));
    }
    pop(st);

    top(st,s);
    if (s) {
        assert(strcmp(s,"a1"));
        my_free(s);
    }
    pop(st);

    puts("[The test was passed!]");
    return(0);
}

int main(){
    server();
//    Test();
    return 1;
}