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

#define PORT 9000
#define BACKLOG 10
#define FILE_PATH "/var/tmp/aesdsocketdata"

int server_fd = -1;
int client_fd = -1;
volatile sig_atomic_t exit_flag = 0;

void cleanup() {
    if (client_fd >= 0) close(client_fd);
    if (server_fd >= 0) close(server_fd);
    remove(FILE_PATH);
    closelog();
}

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        exit_flag = 1;
    }
}

void handle_connection(int client_fd) {
    char buffer[1024];
    int bytes_received;
    FILE *file = fopen(FILE_PATH, "a+");
    if (!file) {
        syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
        return;
    }

    while ((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        fputs(buffer, file);
        if (strchr(buffer, '\n')) {
            break;
        }
    }
    fflush(file);

    fseek(file, 0, SEEK_SET);
    while ((bytes_received = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(client_fd, buffer, bytes_received, 0);
    }

    fclose(file);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int daemon_mode = 0;

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Parse command-line arguments
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    // Bind socket to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        return -1;
    }

    if (daemon_mode) {
        // Daemonize the process
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (pid > 0) {
            // Parent process
            exit(EXIT_SUCCESS);
        }

        // Child process
        if (setsid() < 0) {
            syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // Change working directory to root
        if (chdir("/") < 0) {
            syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    // Start listening on the socket
    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        return -1;
    }

    while (!exit_flag) {
        // Accept a connection
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (errno == EINTR) continue;  // If interrupted by signal, continue loop
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }
        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        // Handle the connection
        handle_connection(client_fd);

        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
        close(client_fd);
        client_fd = -1;
    }

    cleanup();
    return 0;
}
