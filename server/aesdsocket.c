#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include "queue.h" // For managing threads using linked lists

#define PORT 9000
#define BACKLOG 10
#define FILE_PATH "/var/tmp/aesdsocketdata"
#define PIDFILE "/var/run/aesdsocket.pid"

// Mutex for thread synchronization
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_var = PTHREAD_COND_INITIALIZER;

// Linked list to track threads
TAILQ_HEAD(tailhead, thread_data) head;

struct thread_data {
    pthread_t thread_id;
    int client_fd;
    TAILQ_ENTRY(thread_data) entries;
};

int server_fd = -1;
volatile int running = 1; // Control flag for clean exit

void cleanup() {
    // Clean up any remaining threads
    while (!TAILQ_EMPTY(&head)) {
        struct thread_data *t_data = TAILQ_FIRST(&head);
        pthread_join(t_data->thread_id, NULL);
        TAILQ_REMOVE(&head, t_data, entries);
        free(t_data);
    }

    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }

    // Remove the data file
    if (remove(FILE_PATH) != 0) {
        syslog(LOG_ERR, "Failed to remove file: %s", strerror(errno));
    }

    closelog();
}

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal %d", sig);
        running = 0;
        if (server_fd >= 0) {
            syslog(LOG_INFO, "Shutting down server socket");
            shutdown(server_fd, SHUT_RDWR);
        }
    }
}

// Function to handle a new connection, called by a new thread
void* handle_connection(void* thread_data_arg) {
    struct thread_data *t_data = (struct thread_data *)thread_data_arg;
    int client_fd = t_data->client_fd;
    char buffer[1024];
    int bytes_received;

    syslog(LOG_INFO, "Thread %lu: Handling new connection", pthread_self());

    pthread_mutex_lock(&file_mutex);

    FILE *file = fopen(FILE_PATH, "a+");
    if (!file) {
        syslog(LOG_ERR, "Thread %lu: Failed to open file", pthread_self());
        pthread_mutex_unlock(&file_mutex);
        close(client_fd);
        free(t_data);
        return NULL;
    }

    while ((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        syslog(LOG_INFO, "Thread %lu: Received %d bytes", pthread_self(), bytes_received);
        if (fputs(buffer, file) == EOF) {
            syslog(LOG_ERR, "Thread %lu: Failed to write to file", pthread_self());
            break;
        }
        fflush(file);

        if (strchr(buffer, '\n')) {
            break;
        }
    }

    fsync(fileno(file));
    fclose(file);
    syslog(LOG_INFO, "Thread %lu: Finished writing to file", pthread_self());

    file = fopen(FILE_PATH, "r");
    if (file) {
        while ((bytes_received = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            send(client_fd, buffer, bytes_received, 0);
        }
        fclose(file);
    }

    pthread_mutex_unlock(&file_mutex);
    close(client_fd);

    syslog(LOG_INFO, "Thread %lu: Connection closed", pthread_self());

    // Remove the thread from the list
    pthread_mutex_lock(&file_mutex);
    TAILQ_REMOVE(&head, t_data, entries);
    pthread_mutex_unlock(&file_mutex);

    // Signal that this thread has finished
    pthread_mutex_lock(&cond_mutex);
    pthread_cond_signal(&cond_var);
    pthread_mutex_unlock(&cond_mutex);

    free(t_data);
    return NULL;
}

void* timestamp_thread(void* arg) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (running) {
        // Create timestamp string
        time_t now = time(NULL);
        struct tm *time_info = localtime(&now);
        char timestamp[100];
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", time_info);

        // Lock the mutex before writing the timestamp
        pthread_mutex_lock(&file_mutex);
        FILE *file = fopen(FILE_PATH, "a");
        if (file) {
            fputs(timestamp, file);
            fflush(file);
            fclose(file);
        } else {
            syslog(LOG_ERR, "Failed to open file for timestamp: %s", strerror(errno));
        }
        pthread_mutex_unlock(&file_mutex);

        // Calculate the next timestamp time
        ts.tv_sec += 10;

        // Sleep until the next timestamp
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

    }
    return NULL;
}


void write_pid() {
    FILE *pid_file = fopen(PIDFILE, "w");
    if (pid_file) {
        fprintf(pid_file, "%d\n", getpid());
        fclose(pid_file);
    } else {
        syslog(LOG_ERR, "Failed to write PID file: %s", strerror(errno));
    }
}

// Wait for all threads to finish and clean up
void wait_for_all_threads_to_finish() {
    while (!TAILQ_EMPTY(&head)) {
        pthread_mutex_lock(&cond_mutex);
        pthread_cond_wait(&cond_var, &cond_mutex);
        pthread_mutex_unlock(&cond_mutex);

        struct thread_data *t_data = TAILQ_FIRST(&head);
        pthread_join(t_data->thread_id, NULL);
        TAILQ_REMOVE(&head, t_data, entries);
        free(t_data);
    }
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int daemon_mode = 0;
    int optval = 1;

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    TAILQ_INIT(&head); // Initialize the linked list

    // Parse command-line arguments
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        cleanup();
        return -1;
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        syslog(LOG_ERR, "Failed to set socket options: %s", strerror(errno));
        cleanup();
        return -1;
    }

    // Bind socket to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // **Reset the file when the server starts**
    FILE *file = fopen(FILE_PATH, "w");
    if (file) {
        fclose(file);
    } else {
        syslog(LOG_ERR, "Failed to reset file: %s", strerror(errno));
    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        cleanup();
        return -1;
    }

    if (daemon_mode) {
        // Daemonize the process
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
            cleanup();
            exit(EXIT_FAILURE);
        }
        if (pid > 0) {
            // Parent process
            exit(EXIT_SUCCESS);
        }

        // Child process
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            cleanup();
            exit(EXIT_FAILURE);
        }

        // Change working directory to root
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
            cleanup();
            exit(EXIT_FAILURE);
        }

        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        // Write the PID file
        write_pid();
    }

    // Start listening on the socket
    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        cleanup();
        return -1;
    }

    syslog(LOG_INFO, "Server is now listening on port %d", PORT);

    // Start the timestamp thread
    pthread_t ts_thread;
    if (pthread_create(&ts_thread, NULL, timestamp_thread, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create timestamp thread: %s", strerror(errno));
        cleanup();
        return -1;
    }
    
    sleep(10);

    while (running) {
        // Accept a connection
        client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (running) {
                syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            }
            continue;
        }
        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        // Allocate memory for thread data
        struct thread_data *t_data = malloc(sizeof(struct thread_data));
        if (!t_data) {
            syslog(LOG_ERR, "Failed to allocate memory for thread data");
            close(client_fd);
            continue;
        }
        memset(t_data, 0, sizeof(struct thread_data)); // Initialize memory
        t_data->client_fd = client_fd;

        // Create a thread to handle the connection
        if (pthread_create(&t_data->thread_id, NULL, handle_connection, t_data) != 0) {
            syslog(LOG_ERR, "Failed to create thread: %s", strerror(errno));
            close(client_fd);
            free(t_data); // Free the memory if thread creation fails
            continue;
        }

        // Add thread to the linked list
        pthread_mutex_lock(&file_mutex);
        TAILQ_INSERT_TAIL(&head, t_data, entries);
        pthread_mutex_unlock(&file_mutex);
    }

    // Wait for all threads to finish
    wait_for_all_threads_to_finish();

    // Clean up the timestamp thread
    pthread_join(ts_thread, NULL);

    cleanup();
    return 0;
}