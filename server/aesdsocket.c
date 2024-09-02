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
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    if (remove(FILE_PATH) != 0) {
        syslog(LOG_ERR, "Failed to remove file: %s", strerror(errno));
    }
    closelog();
}

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal %d, exiting", sig);
        running = 0;
        if (server_fd >= 0) {
            shutdown(server_fd, SHUT_RDWR);
        }
    }
}

void* handle_connection(void* thread_data_arg) {
    struct thread_data *t_data = (struct thread_data *)thread_data_arg;
    int client_fd = t_data->client_fd;
    char buffer[1024];
    int bytes_received;

    // Lock the mutex before any file operations
    pthread_mutex_lock(&file_mutex);

    // Open the main file for appending and reading
    FILE *file = fopen(FILE_PATH, "a+");
    if (!file) {
        pthread_mutex_unlock(&file_mutex); // Unlock before exiting
        syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
        close(client_fd);
        free(t_data);
        return NULL;
    }

    // Receive data from the client and write it to the main file
    while ((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        fputs(buffer, file);
        fflush(file); // Ensure the data is written to disk

        if (strchr(buffer, '\n')) {
            break;
        }
    }

    // Reset the file pointer to the beginning of the file
    fseek(file, 0, SEEK_SET);

    // Read the entire file content and send it to the client
    while ((bytes_received = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(client_fd, buffer, bytes_received, 0);
    }

    // Close the file
    fclose(file);

    // Unlock the mutex after file operations are complete
    pthread_mutex_unlock(&file_mutex);

    close(client_fd);

    // Remove thread data from the linked list and free memory
    pthread_mutex_lock(&file_mutex);
    TAILQ_REMOVE(&head, t_data, entries);
    pthread_mutex_unlock(&file_mutex);

    free(t_data);
    return NULL;
}

void* timestamp_thread(void* arg) {
    while (running) {
        sleep(10);

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

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        cleanup();
        return -1;
    }

    // **Reset the file when the server starts**
    FILE *file = fopen(FILE_PATH, "w");
    if (file) {
        fclose(file);
    } else {
        syslog(LOG_ERR, "Failed to reset file: %s", strerror(errno));
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

        // Write the PID file on the socket file
        write_pid();
    }

    // Start the timestamp thread (only in the parent process)
    pthread_t ts_thread;
    pthread_create(&ts_thread, NULL, timestamp_thread, NULL);

    // Start listening on the socket
    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        cleanup();
        return -1;
    }

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
        t_data->client_fd = client_fd;

        // Create a thread to handle the connection
        pthread_create(&t_data->thread_id, NULL, handle_connection, t_data);

        // Add thread to the linked list
        pthread_mutex_lock(&file_mutex);
        TAILQ_INSERT_TAIL(&head, t_data, entries);
        pthread_mutex_unlock(&file_mutex);
    }

    // Wait for all threads to finish
    while (!TAILQ_EMPTY(&head)) {
        struct thread_data *t_data = TAILQ_FIRST(&head);
        pthread_join(t_data->thread_id, NULL);
    }

    // Clean up the timestamp thread
    pthread_join(ts_thread, NULL);

    cleanup();
    return 0;
}
