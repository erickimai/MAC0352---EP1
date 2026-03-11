/*   PROTOCOL (text-based, newline-terminated commands):
 *   CREATE <id> <value>   — creates a new resource
 *   GET <id>              — returns the value of a resource
 *   SET <id> <value>      — updates the value of a resource
 *   RESERVE <index>       — reserves the resource at the given index
 *   RELEASE <index>       — releases a reserved resource
 *   LIST                  — lists all existing resources
 *   EXIT                  — closes the client connection
 *
 * EXAMPLE SESSION (client sends):
 *   CREATE cpu 100
 *   LIST
 *   RESERVE 0
 *   RELEASE 0
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

#define PORT "6789"       /* Port the server listens on */
#define BACKLOG 10        /* Max pending connections in the accept queue */
#define MAXDATASIZE 1000  /* Max bytes per message (send and receive) */
#define MAXRESOURCES 100  /* Max number of resources the server can hold */

/*
 * Resource — represents a single named shared resource.
 *
 * Fields:
 *   reserved    — 0 if free, 1 if reserved
 *   id          — unique string identifier (e.g. "cpu", "memory")
 *   value       — associated value string (e.g. "100", "available")
 *   reserved_by — sockfd of the client holding the reservation, or 0 if free
 *
 * Example:
 *   { reserved=1, id="cpu", value="100", reserved_by=5 }
 */
typedef struct {
    int reserved;
    char *id;
    char *value;
    int reserved_by;
} Resource;

/*
 * ThreadArgs — arguments passed to each client-handling thread.
 *
 * Fields:
 *   sockfd    — the connected client's socket file descriptor
 *   lock      — pointer to the shared mutex protecting the resources array
 *   resources — pointer to the shared array of resources
 */
typedef struct {
    int sockfd;
    pthread_mutex_t *lock;
    Resource *resources;
} ThreadArgs;

/*
 * get_in_addr — returns a pointer to the IPv4 or IPv6 address in a sockaddr.
 * Used to convert the address to a human-readable string via inet_ntop().
 */
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
 * parser — thread function that handles all communication with one client.
 *
 * Receives commands in a loop until the client disconnects or sends EXIT.
 * Each command is parsed token-by-token using strtok().
 * The shared resources array is protected by a mutex on every access.
 *
 * Example commands and expected responses:
 *   "CREATE disk 500"  → server replies "resource created\n"
 *   "LIST"             → server replies formatted list of all resources
 *   "RESERVE 0"        → server replies "resource reserved\n"
 *   "RELEASE 0"        → server replies "resource released\n"
 *   "EXIT"             → server replies "connection ended\n" and closes socket
 */
void *parser(void *args) {
    ThreadArgs *a = (ThreadArgs *)args;
    char buf[MAXDATASIZE];
    int numbytes;
    char *token;

    /*
     * Main receive loop — runs until the client disconnects (recv returns 0)
     * or an error occurs (recv returns -1).
     * On abrupt disconnect, all resources reserved by this client are released below.
     */
    while((numbytes = recv(a->sockfd, buf, MAXDATASIZE - 1, 0)) > 0) {
        buf[numbytes] = '\0';
        printf("received : %s\n", buf);

        /* Tokenize the incoming message by spaces */
        token = strtok(buf, " ");

        while (token != NULL) {
            pthread_mutex_lock(a->lock); /* Lock before accessing shared resources */

            /* ---- GET <id> ----
             * Retrieve and send back the value of the named resource.
             * Example: "GET cpu\n" → "100\n"
             */
            if (strcmp(token, "GET") == 0) {
                token = strtok(NULL, " ");
                /* TODO: search resources by id and send back value */
            }

            /* ---- CREATE <id> <value> ----
             * Creates a new resource with the given id and value.
             * Example: "CREATE cpu 100\n" → "resource created\n"
             */
            else if (strcmp(token, "CREATE") == 0) {
                token = strtok(NULL, " ");           /* token = id */
                Resource new_resource;
                new_resource.id = strdup(token);
                token = strtok(NULL, "\n");          /* token = value (rest of line) */
                new_resource.value = strdup(token);
                new_resource.reserved_by = a->sockfd; /* track creator */

                int i;
                for (i = 0; i < MAXRESOURCES; i++) {
                    if (a->resources[i].id == NULL) { /* find first empty slot */
                        a->resources[i] = new_resource;
                        if (send(a->sockfd, "resource created\n", 16, 0) == -1)
                            perror("send");
                        break;
                    }
                }
                /* No empty slot found */
                if (i == MAXRESOURCES)
                    if (send(a->sockfd, "ERROR: full of resources\n", 24, 0) == -1)
                        perror("send");
            }

            /* ---- SET <id> <value> ----
             * Updates the value of an existing resource.
             * Example: "SET cpu 200\n" → "resource updated\n"
             */
            else if (strcmp(token, "SET") == 0) {
                token = strtok(NULL, " ");
                /* TODO: find resource by id, update value, send confirmation */
            }

            /* ---- RESERVE <index> ----
             * Marks the resource at <index> as reserved by this client.
             * Example: "RESERVE 0\n" → "resource reserved\n"
             *          "RESERVE 0\n" (already reserved) → "ERROR: Resource already reserved\n"
             */
            else if (strcmp(token, "RESERVE") == 0) {
                token = strtok(NULL, "\n");           /* token = index as string */
                int found = 0;
                for (int i = 0; i < MAXRESOURCES; i++) {
                    if (a->resources[i].id != NULL && !strcmp(token, a->resources[i].id)) {
                        if (a->resources[i].reserved == 0) {
                            a->resources[i].reserved = 1;
                            a->resources[i].reserved_by = a->sockfd;
                            if (send(a->sockfd, "resource reserved\n", 17, 0) == -1)
                                perror("send");
                        } else {
                            if (send(a->sockfd, "ERROR: Resource already reserved\n", 32, 0) == -1)
                                perror("send");
                        }
                        found = 1;
                        break;
                    }
                }
                if (found == 0)
                    if (send(a->sockfd, "ERROR: Resource doesn`t exist\n", 30, 0) == -1)
                        perror("send");
            }

            /* ---- RELEASE <index> ----
             * Frees a previously reserved resource.
             * Example: "RELEASE 0\n" → "resource released\n"
             *          "RELEASE 0\n" (not reserved) → "ERROR: Resource not reserved\n"
             */
            else if (strcmp(token, "RELEASE") == 0) {
                token = strtok(NULL, "\n");            /* token = index as string */
                int found = 0;
                for (int i = 0; i < MAXRESOURCES; i++) {
                    if (a->resources[i].id != NULL && !strcmp(token, a->resources[i].id)) {
                        if (a->resources[i].reserved == 1) {
                            a->resources[i].reserved = 0;
                            if (send(a->sockfd, "resource released\n", 17, 0) == -1)
                                perror("send");
                        }
                        else { 
                            if (send(a->sockfd, "ERROR: Resource not reserved\n", 28, 0) == -1)
                                perror("send");
                        }
                        found = 1;
                    }
                }
                if (found == 0) {
                    if (send(a->sockfd, "ERROR: Resource doesn`t exist\n", 30, 0) == -1)
                        perror("send");
                }
            }
            /* ---- LIST ----
             * Sends a formatted list of all existing resources.
             * Example response:
             *   "id: cpu \nvalue: 100 \nreserved: 0\n\n
             *    id: disk \nvalue: 500 \nreserved: 1\n\n"
             */
            else if (strcmp(token, "LIST\n") == 0) {
                char response[MAXDATASIZE] = "";
                char buffer[MAXDATASIZE] = "";

                for (int i = 0; i < MAXRESOURCES; i++) {
                    if (a->resources[i].id != NULL) {
                        /* Format each resource into response, then append to buffer */
                        size_t space_left = sizeof(buffer) - strlen(buffer) - 1;
                        snprintf(response, sizeof(response),
                            "id: %s \nvalue: %s \nreserved: %d\n\n",
                            a->resources[i].id,
                            a->resources[i].value,
                            a->resources[i].reserved);
                        strncat(buffer, response, space_left);
                    }
                }

                if (send(a->sockfd, buffer, strlen(buffer), 0) == -1)
                    perror("send");
            }

            /* ---- EXIT ----
             * Client requests a clean disconnection.
             * Server sends confirmation, unlocks, frees memory, and exits thread.
             * Example: "EXIT\n" → "connection ended\n"
             */
            else if (strcmp(token, "EXIT\n") == 0) {
                if (send(a->sockfd, "connection ended\n", 17, 0) == -1)
                    perror("send");
                close(a->sockfd);
                pthread_mutex_unlock(a->lock);
                free(a);
                return NULL;
            }

            /* ---- Unknown command ----
             * Sends an error message back to the client.
             */
            else {
                fprintf(stderr, "invalid request \n");
                pthread_mutex_unlock(a->lock);
                if (send(a->sockfd, "ERROR: invalid request\n", 22, 0) == -1)
                    perror("send");
                break;
            }

            token = strtok(NULL, " ");
            pthread_mutex_unlock(a->lock);
            break;
        }
    }

    /*
     * Client disconnected abruptly (recv returned 0 or error).
     * Release all resources this client had reserved so other clients can use them.
     * Then close socket and free thread memory.
     */
    pthread_mutex_lock(a->lock);
    for (int i = 0; i < MAXRESOURCES; i++) {
        if (a->resources[i].reserved_by == a->sockfd) {
            a->resources[i].reserved = 0;
            a->resources[i].reserved_by = 0;
            printf("server: auto-released resource '%s' after client disconnect\n",
                   a->resources[i].id);
        }
    }
    pthread_mutex_unlock(a->lock);

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
    Resource resources[MAXRESOURCES];          /* Shared resource array */
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; /* Protects resources array */

    memset(&hints, 0, sizeof hints);
    memset(resources, 0, sizeof(resources));   /* Initialize all resource slots to NULL/0 */

    hints.ai_family = AF_UNSPEC;               /* Accept IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;           /* TCP */
    hints.ai_flags = AI_PASSIVE;               /* Bind to local address */

    /* Resolve local address for binding */
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "gai error: %s\n", gai_strerror(rv));
        return 1;
    }

    /* Loop through results and bind to the first one that works */
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        /* Allow reuse of port immediately after server restarts */
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
    /*
     * Main accept loop — waits for incoming connections.
     * Each new client gets its own detached thread running parser().
     * ThreadArgs is heap-allocated so each thread has its own copy.
     */
    while (1) {
        pthread_t thread;
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        /* Build thread arguments — freed by the thread when done */
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->sockfd = new_fd;
        args->lock = &lock;
        args->resources = resources;

        pthread_create(&thread, NULL, parser, args);

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("server: got connection from %s (sockfd=%d)\n", s, new_fd);
        fflush(stdout);

        /* Detach so thread cleans itself up when it returns */
        pthread_detach(thread);
    }

    return 0;
}
