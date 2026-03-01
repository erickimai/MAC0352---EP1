#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

#define PORT "6789"

#define BACKLOG 10

#define MAXDATASIZE 100

#define MAXRESOURCES 100

typedef struct {
    int reserved;
    char *id; 
    char *value;
} Resource;

typedef struct {
    int sockfd;
    pthread_mutex_t *lock;
    Resource *resources;
} ThreadArgs;

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *parser(void *args) {
    ThreadArgs *a = (ThreadArgs *)args;
    char buf[MAXDATASIZE];
    int numbytes;
    char *token;

    if ((numbytes = recv(a->sockfd, buf, MAXDATASIZE - 1, 0)) == -1) {
        perror("recv");
        exit(1);
    }

    buf[numbytes] = '\0';
    printf("received : %s\n", buf);

    token = strtok(buf, " ");

    while (token != NULL) {
        pthread_mutex_lock(a->lock);
        if (strcmp(token, "GET") == 0) {
            token = strtok(NULL, " ");
            //return resource->value
        }
        else if (strcmp(token, "CREATE") == 0) {
            token = strtok(NULL, " ");
            Resource new_resource;
            new_resource.id = strdup(token);
            token = strtok(NULL, "\n");
            new_resource.value = strdup(token);
            int i;
            for (i = 0; i < MAXRESOURCES; i++) {
                if (a->resources[i].id == NULL) {
                    a->resources[i] = new_resource;
                    if (send(a->sockfd, "resource created", 16, 0) == -1)
                        perror("send");
                    break;
                }
            }
            if (i == MAXRESOURCES) 
               if (send(a->sockfd, "ERROR: full of resources", 24, 0) == -1)    
                    perror("send");
        }
        else if (strcmp(token, "SET") == 0) {
            token = strtok(NULL, " ");
            //set resource
        }
        else if (strcmp(token, "RESERVE") == 0) {
            token = strtok(NULL, " ");
            //reserve resource
        }
        else if (strcmp(token, "RELEASE") == 0) {
            token = strtok(NULL, " ");
            //release resource
        }
        else if (strcmp(token, "LIST") == 0) {
            token = strtok(NULL, " ");
            //list resource
        }
        else {
            fprintf(stderr, "invalid request \n");
            exit(1);
        }
        token = strtok(NULL, " ");
        pthread_mutex_unlock(a->lock);
    }
    free(a);
}

int main (void) {
    int sockfd, new_fd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; 
    socklen_t sin_size;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    Resource resources[100]; 
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

    memset(&hints, 0, sizeof hints);
    memset(resources, 0, sizeof(resources));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, "6789", &hints, &servinfo)) != 0) {
        fprintf(stderr, "gai error: %s\n", gai_strerror(rv));
        return(1);
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
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

    freeaddrinfo(servinfo);

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1) {
        pthread_t thread;
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->sockfd = new_fd;
        args->lock = &lock;
        args->resources = resources;

        pthread_create(&thread, NULL, parser, args);
        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);

        pthread_detach(thread);
    }

    return 0;
}

