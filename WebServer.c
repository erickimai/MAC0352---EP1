/*   PROTOCOL (text-based, newline-terminated commands):
 *   CREATE <id> <value>   — creates a new resource
 *   GET <id>              — returns the value of a resource
 *   SET <id> <value>      — updates the value of a resource
 *   RESERVE <id>          — reserves the resource with the given id   
 *   RELEASE <id>          — releases a reserved resource             
 *   LIST                  — lists all existing resources
 *   EXIT                  — closes the client connection
 *
 * EXAMPLE SESSION (client sends):
 *   CREATE cpu 100
 *   LIST
 *   RESERVE cpu
 *   RELEASE cpu
 *   EXIT
 */

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
#define MAXDATASIZE 1000
#define MAXRESOURCES 100
#define LOGFILE "WebServer.log"

typedef struct {
    int reserved;
    char *id;
    char *value;
    int reserved_by;
} Resource;

typedef struct {
    int sockfd;
    pthread_mutex_t *lock;
    Resource *resources;
} ThreadArgs;

void logMessage(char* msg){
    FILE* f = fopen(LOGFILE,"a");
    
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    fprintf(f,"[%d-%02d-%02d %02d:%02d:%02d]:\t",tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    fprintf(f,"%s\n",msg);

    fclose(f);
}

// returns the resource by the id, note that if id is NULL returns the first resouce "empty"
Resource *findResource(Resource *resources,char* id){
    for (int i = 0; i < MAXRESOURCES; i++) {
        if ((resources[i].id != NULL && id != NULL && strcmp(id, resources[i].id) == 0) ||
            (resources[i].id == NULL && id == NULL)) 
            return &resources[i];
    }
    return NULL;
}

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

    char *saveptr;

    while ((numbytes = recv(a->sockfd, buf, MAXDATASIZE - 1, 0)) > 0) {
        buf[numbytes] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';

        printf("received: [%s]\n", buf); /* brackets help spot hidden whitespace */

        token = strtok_r(buf, " ", &saveptr); 

        pthread_mutex_lock(a->lock);

        if(token == NULL) { /* empty line — ignore and wait for next recv */
            logMessage("ERROR: invalid request null message");
            if (send(a->sockfd, "ERROR: invalid request\n",
                     strlen("ERROR: invalid request\n"), 0) == -1) 
                perror("send");
            pthread_mutex_unlock(a->lock);

            continue;
        } 


        /* ---- GET <id> ---- */
        if (strcmp(token, "GET") == 0) {
            char *id_token = strtok_r(NULL, " ", &saveptr);

            if (id_token == NULL) {
                send(a->sockfd, "ERROR: usage: GET <id>\n", 
                     strlen("ERROR: usage: GET <id>\n"), 0);
                pthread_mutex_unlock(a->lock);
                continue;
            }

            char response[MAXDATASIZE];
            int found = 0;

            for (int i = 0; i < MAXRESOURCES; i++) {
                if (a->resources[i].id != NULL &&
                    strcmp(id_token, a->resources[i].id) == 0) {
                    
                    if (a->resources[i].value == NULL) {
                        if (send(a->sockfd, "resource found with NULL value\n",
                                strlen("resource found with NULL value\n"), 0) == -1)
                            perror("send");
                    }
                    else {
                        snprintf(response, sizeof(response),
                                "resource with id %s has value %s\n",
                                id_token,
                                a->resources[i].value);
                    }
                    found = 1;
                    break; /* value is unique — stop after first match */
                }
            }

            if (found == 0) {
                logMessage("ERROR: Client tried to get a resource that doesn't exit");
                if (send(a->sockfd, "ERROR: Resource doesn't exist\n",
                         strlen("ERROR: Resource doesn't exist\n"), 0) == -1) 
                    perror("send");
            }
            else {
                logMessage("Client got a resource");
                if (send(a->sockfd, response,
                        strlen(response), 0) == -1)
                    perror("send");
            }
        }

        /* ---- CREATE <id> <value> ---- */
        else if (strcmp(token, "CREATE") == 0) {
            char *id_token    = strtok_r(NULL, " ", &saveptr);  
            char *value_token = strtok_r(NULL, "\n", &saveptr); /* rest of line */

            if (id_token == NULL || value_token == NULL) {
                send(a->sockfd, "ERROR: usage: CREATE <id> <value>\n",
                     strlen("ERROR: usage: CREATE <id> <value>\n"), 0);
                pthread_mutex_unlock(a->lock);
                continue;
            }

            Resource new_resource;
            new_resource.reserved    = 0;             
            new_resource.reserved_by = 0;             
            new_resource.id          = strdup(id_token);
            new_resource.value       = strdup(value_token);

            if (findResource(a->resources, new_resource.id) != NULL)
            {
                logMessage("ERROR: Client tried to create a new resource with an id already used");
                if (send(a->sockfd, "ERROR: id already used\n",
                         strlen("ERROR: id already used\n"), 0) == -1)
                    perror("send");

                pthread_mutex_unlock(a->lock);
                continue;
            }
            
            Resource *r = findResource(a->resources, NULL);

            if (r == NULL) {
                free(new_resource.id);    /* avoid leak if we couldn't store it */
                free(new_resource.value);
                logMessage("ERROR: Client tried to create a new resource but the base is full");
                if (send(a->sockfd, "ERROR: full of resources\n",
                         strlen("ERROR: full of resources\n"), 0) == -1)
                    perror("send");
                pthread_mutex_unlock(a->lock);
                continue;
            }

            *r = new_resource;
            logMessage("Client create a new resource");
            if (send(a->sockfd, "resource created\n",
                        strlen("resource created\n"), 0) == -1) 
                perror("send");

            
        }

        /* ---- SET <id> <value> ---- */
        else if (strcmp(token, "SET") == 0) {
            char *id_token    = strtok_r(NULL, " ", &saveptr);  
            char *value_token = strtok_r(NULL, "\n", &saveptr); /* rest of line */

            if (id_token == NULL || value_token == NULL) {
                send(a->sockfd, "ERROR: usage: SET <id> <value>\n",
                     strlen("ERROR: usage: SET <id> <value>\n"), 0);
                pthread_mutex_unlock(a->lock);
                continue;
            }

            int i;
            for (i = 0; i < MAXRESOURCES; i++) {
                if (a->resources[i].id != NULL &&
                    strcmp(id_token, a->resources[i].id) == 0) {

                    if (a->resources[i].reserved_by == a->sockfd) {
                        free(a->resources[i].value);
                        a->resources[i].value = strdup(value_token);
                        logMessage("Client changed the value of a resource");
                        if (send(a->sockfd, "value changed\n",
                                strlen("value changed\n"), 0) == -1) 
                            perror("send");
                        break;
                    }
                    else {
                        logMessage("ERROR: Client tried to change the value of a resource without own reservation");
                        if (a->resources[i].reserved == 0) {
                            if (send(a->sockfd, "ERROR: resource not reserved\n",
                                    strlen("ERROR: resource not reserved\n"), 0) == -1) 
                                perror("send");
                            break;
                        } else {
                            if (send(a->sockfd, "ERROR: resource not reserved by you\n",
                                    strlen("ERROR: resource not reserved by you\n"), 0) == -1) 
                                perror("send");
                            break;
                        }
                    }
                }
            }
            
            if (i == MAXRESOURCES) {
                logMessage("ERROR: Client tried to change the value of a resource that doesn't exit");
                if (send(a->sockfd, "ERROR: resource not found\n",
                        strlen("ERROR: resource not found\n"), 0) == -1)
                    perror("send");
            }
        }

        else if (strcmp(token, "RESERVE") == 0) {
            char *id_token = strtok_r(NULL, " ", &saveptr);
            int found = 0;

            for (int i = 0; i < MAXRESOURCES; i++) {
                if (a->resources[i].id != NULL &&
                    strcmp(id_token, a->resources[i].id) == 0) {

                    if (a->resources[i].reserved == 0) {
                        a->resources[i].reserved    = 1;
                        a->resources[i].reserved_by = a->sockfd;
                        logMessage("Client reserved a resource");
                        if (send(a->sockfd, "resource reserved\n",
                                 strlen("resource reserved\n"), 0) == -1) 
                            perror("send");
                    } else {
                        logMessage("ERROR: Client tried to reserve an already reserved resource");
                        if (send(a->sockfd, "ERROR: Resource already reserved\n",
                                 strlen("ERROR: Resource already reserved\n"), 0) == -1) 
                        perror("send");
                    }
                    found = 1;
                    break;
                }
            }

            if (found == 0) {
                logMessage("ERROR: Client tried to reserve non existent resource");
                if (send(a->sockfd, "ERROR: Resource doesn't exist\n",
                         strlen("ERROR: Resource doesn't exist\n"), 0) == -1) 
                    perror("send");
            }
        }

        /* ---- RELEASE <id> ---- */
        else if (strcmp(token, "RELEASE") == 0) {
            char *id_token = strtok_r(NULL, " ", &saveptr);
            int found = 0;

            for (int i = 0; i < MAXRESOURCES; i++) {
                if (a->resources[i].id != NULL &&
                    strcmp(id_token, a->resources[i].id) == 0) {

                    if (a->resources[i].reserved == 1) {
                        a->resources[i].reserved    = 0;
                        a->resources[i].reserved_by = 0; 
                        logMessage("Client released resource");
                        if (send(a->sockfd, "resource released\n",
                                 strlen("resource released\n"), 0) == -1)
                            perror("send");
                    } else {
                        logMessage("ERROR: Client tried to release non reserved resource");
                        if (send(a->sockfd, "ERROR: Resource not reserved\n",
                                 strlen("ERROR: Resource not reserved\n"), 0) == -1) 
                            perror("send");
                    }
                    found = 1;
                    break; /* id is unique — stop after first match */
                }
            }

            if (found == 0) {
                logMessage("ERROR: Client tried to release non existent resource");
                if (send(a->sockfd, "ERROR: Resource doesn't exist\n",
                         strlen("ERROR: Resource doesn't exist\n"), 0) == -1) 
                    perror("send");
            }
        }

        else if (strcmp(token, "LIST") == 0) {
            char response[MAXDATASIZE] = "";
            char entry[256];
            int any = 0;

            for (int i = 0; i < MAXRESOURCES; i++) {
                if (a->resources[i].id != NULL) {
                    snprintf(entry, sizeof(entry),
                        "id: %s | value: %s | reserved: %d\n",
                        a->resources[i].id,
                        a->resources[i].value,
                        a->resources[i].reserved);

                    /*
                     * Guard against overflow: only append if there's room.
                     * sizeof(response) - strlen(response) - 1 = remaining bytes.
                     */
                    strncat(response, entry,
                            sizeof(response) - strlen(response) - 1);
                    any = 1;
                }
            }
            
            if (!any) {
                strncpy(response, "no resources\n", sizeof(response) - 1);
            }

            if (send(a->sockfd, response, strlen(response), 0) == -1) 
                perror("send");
            logMessage("Client listed the resources");
        }

        else if (strcmp(token, "EXIT") == 0) {
            if (send(a->sockfd, "connection ended\n",
                     strlen("connection ended\n"), 0) == -1) 
                perror("send");

            pthread_mutex_unlock(a->lock);
            close(a->sockfd);
            free(a);
            logMessage("Connection Closed");
            return NULL;
        }

        /* ---- Unknown command ---- */
        else {
            fprintf(stderr, "invalid request: [%s]\n", token);
            if (send(a->sockfd, "ERROR: invalid request\n",
                     strlen("ERROR: invalid request\n"), 0) == -1) 
                perror("send");
        }

        pthread_mutex_unlock(a->lock);

    } /* end recv loop */

    /*
     * Client disconnected (recv returned 0) or error (recv returned -1).
     *
     * Auto-release all resources this client reserved.
     */
    pthread_mutex_lock(a->lock);
    for (int i = 0; i < MAXRESOURCES; i++) {
        if (a->resources[i].reserved_by == a->sockfd) {
            a->resources[i].reserved    = 0;
            a->resources[i].reserved_by = 0;
            char l[100];
            sprintf(l,"auto-released '%s' after client disconnect",a->resources[i].id);
            logMessage(l);
            printf("server: auto-released '%s' after client disconnect\n",
                   a->resources[i].id);
        }
    }
    pthread_mutex_unlock(a->lock);
    char l[100];
    sprintf(l,"client on sockfd %d disconnected\n", a->sockfd);
    logMessage(l);

    printf("server: client on sockfd %d disconnected\n", a->sockfd);
    close(a->sockfd);
    free(a);
    return NULL;
}

int main(void) {
    int sockfd, new_fd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    Resource resources[MAXRESOURCES];
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

    memset(&hints, 0, sizeof hints);
    memset(resources, 0, sizeof(resources));

    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "gai error: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
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

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    printf("server: waiting for connections on port %s...\n", PORT);
    fflush(stdout);

    while (1) {
        pthread_t thread;
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->sockfd    = new_fd;
        args->lock      = &lock;
        args->resources = resources;

        pthread_create(&thread, NULL, parser, args);

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("server: got connection from %s (sockfd=%d)\n", s, new_fd);
        char l[100];
        sprintf(l,"got connection from %s (sockfd=%d)", s, new_fd);
        logMessage(l);
        fflush(stdout);

        pthread_detach(thread);
    }

    return 0;
}