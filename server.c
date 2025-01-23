#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "threadpool.h"

#define BUFFER_SIZE 4000
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
#define RESPONSE_SIZE 65535
time_t now;
char timebuf[128];

char *get_mime_type(char *name) {
    char *ext = strrchr(name, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".au") == 0) return "audio/basic";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    return NULL;
}

char* getFullPath(const char* givenPath) {
    if (givenPath == NULL) {
        return NULL;
    }

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
        return NULL;
    }

    size_t cwdLen = strlen(cwd);
    size_t pathLen = strlen(givenPath);
    size_t fullPathSize = cwdLen + 1 + pathLen + 1;

    char* fullPath = (char*)malloc(fullPathSize * sizeof(char));
    if (fullPath == NULL) {
        perror("malloc() error");
        return NULL;
    }

    // Construct the full path
    snprintf(fullPath, fullPathSize, "%s%s", cwd, givenPath);

    return fullPath;
}

// Function to check if a path ends with a '/'
bool ends_with_slash(const char* path) {
    size_t len = strlen(path);
    return (len > 0 && path[len - 1] == '/');
}

// Function to read the first line of a request from a client socket
char* read_request(int client_socket) {
    char buffer[BUFFER_SIZE] = {0};
    char* first_line = NULL;
    ssize_t bytes_read;
    size_t buffer_len = 0;

    // Read data until "\r\n" is found or the buffer is full
    while (buffer_len < BUFFER_SIZE - 1) {
        bytes_read = read(client_socket, buffer + buffer_len, BUFFER_SIZE - 1 - buffer_len);
        if (bytes_read < 0) {
            perror("read");
            return NULL;
        } else if (bytes_read == 0) {
            printf("Client disconnected\n");
            return NULL;
        }

        buffer_len += bytes_read;

        // Check if the received data contains "\r\n"
        if (strstr(buffer, "\r\n") != NULL) {
            first_line = strdup(buffer);
            break;
        }
    }

    // Null-terminate the received data at the position of "\r\n"
    char* crlf_pos = strstr(buffer, "\r\n");
    if (crlf_pos != NULL) {
        *crlf_pos = '\0';
    }
    else printf("didnt find a crlf");

    return first_line;
}

// Function to send an HTTP error response
char* handle_error_response(int error_type, const char* path, const char* mime_type) {
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));

    // Allocate memory for the final error message
    char* error_message = (char*)malloc(RESPONSE_SIZE);
    if (error_message == NULL) {
        perror("malloc");
        return NULL;
    }

    // Template for the HTTP response
    const char *response_template =
            "HTTP/1.0 %d %s\r\n"
            "Server: webserver/1.0\r\n"
            "Date: %s\r\n"
            "%s" // Optional headers (e.g., Location for 302)
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n"
            "<BODY><H4>%d %s</H4>\r\n"
            "%s\r\n"
            "</BODY></HTML>\r\n";

    // Define error details
    const char *status_message = NULL;
    const char *body_content = NULL;
    char *optional_headers = "";

    switch (error_type) {
        case 302: // 302 Found (Directory does not end with '/')
            status_message = "Found";
            body_content = "Directories must end with a slash.";
            mime_type = "text/html";
            optional_headers = (char*)malloc(256); // Allocate memory for Location header
            if (optional_headers == NULL) {
                perror("malloc");
                //fprintf(stderr,"Freeing pointer: error massage\n");
                free(error_message);
                return NULL;
            }
            snprintf(optional_headers, 256, "Location: %s/\r\n", path);
            break;

        case 400: // 400 Bad Request
            status_message = "Bad Request";
            body_content = "Bad Request.";
            mime_type = "text/html";
            break;

        case 403: // 403 Forbidden
            status_message = "Forbidden";
            body_content = "Access denied.";
            mime_type = "text/html";
            break;

        case 404: // 404 Not Found
            status_message = "Not Found";
            body_content = "File not found.";
            mime_type = "text/html";
            break;

        case 500: // 500 Internal Server Error
            status_message = "Internal Server Error";
            body_content = "Some server side error.";
            mime_type = "text/html";
            break;

        case 501: // 501 Not Supported
            status_message = "Not supported";
            body_content = "Method is not supported.";
            mime_type = "text/html";
            break;

        default: // Unknown error type
            status_message = "Internal Server Error";
            body_content = "Some server side error.";
            mime_type = "text/html";
            break;
    }

    // Calculate the content length (only the HTML body)
    const char* html_template =
            "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n"
            "<BODY><H4>%d %s</H4>\r\n"
            "%s\r\n"
            "</BODY></HTML>\r\n";

    char html_body[512];
    snprintf(html_body, sizeof(html_body), html_template,
             error_type, status_message, error_type, status_message, body_content);

    size_t content_length = strlen(html_body);

    // Format the final response
    snprintf(error_message, RESPONSE_SIZE, response_template,
             error_type, status_message, timebuf, optional_headers,mime_type, content_length,
             error_type, status_message, error_type, status_message, body_content);

    if (strcmp(optional_headers,"") != 0) {
        //fprintf(stderr,"Freeing pointer: optional headers\n");
        free(optional_headers);
    }
    //fprintf(stderr,"full message: %s\n",error_message);
    return error_message;
}

// Function to check if the requested path exists
char* check_path(const char* path) {
    // Open the current directory
    char* mime_type = get_mime_type((char*)path);
    DIR* dir = opendir(".");
    if (dir == NULL) {
        perror("opendir");
        return handle_error_response(500, NULL, mime_type);
    }
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        // Path does not exist
        closedir(dir);
        return handle_error_response(404, NULL, mime_type); // 404 Not Found
    }
    // Check if the path is a directory
    if (S_ISDIR(path_stat.st_mode)) {
        if (!ends_with_slash(path)) {
            closedir(dir);
            return handle_error_response(302, path, mime_type);
        }
    }

    // Path exists
    closedir(dir);
    return NULL; // No error
}

// Function to generate directory listing in HTML format
char* generate_directory_listing(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        perror("opendir");
        return NULL;
    }

    // Preallocate a buffer for the entire directory listing
    char* html_body = (char*)malloc(RESPONSE_SIZE);
    if (html_body == NULL) {
        perror("malloc");
        closedir(dir);
        return NULL;
    }

    // HTML header
    snprintf(html_body, RESPONSE_SIZE,
             "<HTML>\n"
             "<HEAD><TITLE>Index of %s</TITLE></HEAD>\n"
             "<BODY>\n"
             "<H4>Index of %s</H4>\n"
             "<table CELLSPACING=8>\n"
             "<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\n",
             path, path);

    // Iterate through directory entries
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat entry_stat;
        if (stat(full_path, &entry_stat) != 0) {
            continue; // Skip if stat fails
        }

        // Add entry to the HTML table
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&entry_stat.st_mtime));

        char entity_line[1024]; // Buffer for each entity line
        if (S_ISDIR(entry_stat.st_mode)) {
            // Directory entry
            snprintf(entity_line, sizeof(entity_line),
                     "<tr><td><A HREF=\"%s/\">%s/</A></td><td>%s</td><td></td></tr>\n",
                     entry->d_name, entry->d_name, timebuf);
        } else {
            // File entry
            snprintf(entity_line, sizeof(entity_line),
                     "<tr><td><A HREF=\"%s\">%s</A></td><td>%s</td><td>%ld</td></tr>\n",
                     entry->d_name, entry->d_name, timebuf, entry_stat.st_size);
        }

        // Append the entity line to the HTML body
        strncat(html_body, entity_line, RESPONSE_SIZE - strlen(html_body) - 1);
    }

    // Close the HTML response
    strncat(html_body,
            "</table>\n"
            "<HR>\n"
            "<ADDRESS>webserver/1.0</ADDRESS>\n"
            "</BODY></HTML>\n",
            RESPONSE_SIZE - strlen(html_body) - 1);

    closedir(dir);
    return html_body;
}

// Function to handle file responses
char* handle_file_response(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL; // File cannot be opened
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate memory for the file data
    char* file_data = malloc(file_size + 1);
    if (file_data == NULL) {
        fclose(file);
        return NULL;
    }

    // Read file data
    fread(file_data, 1, file_size, file);
    file_data[file_size] = '\0';
    fclose(file);

    // Generate the HTTP response
    char* response = malloc(RESPONSE_SIZE);
    if (response == NULL) {
        free(file_data);
        return NULL;
    }

    // Get MIME type
    char* mime_type = get_mime_type((char*)path);
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));


    // Format the HTTP response
    if (mime_type != NULL) {
        // Include Content-Type header if mime_type is not NULL
        snprintf(response, RESPONSE_SIZE,
                 "HTTP/1.0 200 OK\r\n"
                 "Server: webserver/1.0\r\n"
                 "Date: %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %ld\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%s",
                 timebuf, mime_type, file_size, file_data);
    } else {
        // Exclude Content-Type header if mime_type is NULL
        snprintf(response, RESPONSE_SIZE,
                 "HTTP/1.0 200 OK\r\n"
                 "Server: webserver/1.0\r\n"
                 "Date: %s\r\n"
                 "Content-Length: %ld\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%s",
                 timebuf, file_size, file_data);
    }
    free(file_data);
    return response;
}

// Function to handle OK responses
char* handle_ok_response(const char* path) {
    char* mime_type = get_mime_type((char*) path);
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        return NULL; // Path does not exist
    }

    if (S_ISDIR(path_stat.st_mode)) {
        // Path is a directory
        if (!ends_with_slash(path)) {
            // Directory does not end with '/', return 302 Found
            return handle_error_response(302, path, mime_type);
        }

        // Check for index.html in the directory
        char index_path[PATH_MAX];
        snprintf(index_path, sizeof(index_path), "%s/index.html", path);

        if (stat(index_path, &path_stat) == 0 && S_ISREG(path_stat.st_mode)) {
            // index.html exists and is a regular file
            return handle_file_response(index_path);
        } else {
            // No index.html, generate directory listing
            char* html_body = generate_directory_listing(path);
            if (html_body == NULL) {
                return NULL; // Failed to generate directory listing
            }

            // Generate the 200 OK response with the directory listing
            char* response = (char*)malloc(RESPONSE_SIZE);
            if (response == NULL) {
               // fprintf(stderr,"Freeing pointer: html body\n");
                free(html_body);
                return NULL;
            }

            // Format the HTTP response
            snprintf(response, RESPONSE_SIZE,
                     "HTTP/1.0 200 OK\r\n"
                     "Server: webserver/1.0\r\n"
                     "Date: %s\r\n"
                     "Content-Type: text/html\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     timebuf, strlen(html_body), html_body);
            //fprintf(stderr,"Freeing pointer: html body\n");
            free(html_body);
            return response;
        }
    } else if (S_ISREG(path_stat.st_mode)) {
        // Path is a file
        if ((path_stat.st_mode & S_IRUSR) == 0) {
            // File does not have read permissions
            return handle_error_response(403, NULL, mime_type);
        }

        // Return the file
        return handle_file_response(path);
    } else {
        // Path is not a regular file or directory
        return handle_error_response(403, NULL, mime_type);
    }
}

// Function to check the first line of the HTTP request
char* request_handler(const char* request) {
    char method[32] = {0}, path[256] = {0}, protocol[32] = {0};

    // Parse the first line of the request
    int tokens = sscanf(request, "%31s %255s %31s", method, path, protocol);

    // Invalid number of tokens
    if (tokens != 3) {
        return handle_error_response(400, NULL, NULL);
    }

    char* final_path = getFullPath(path);
    char* mime_type = get_mime_type(final_path);

    // Validate the protocol
    if (strcmp(protocol, "HTTP/1.0") != 0 && strcmp(protocol, "HTTP/1.1") != 0) {
        free(final_path); // Free final_path before returning
        return handle_error_response(400, NULL, mime_type);
    }

    // Validate the method (only GET is supported)
    if (strcmp(method, "GET") != 0) {
        free(final_path); // Free final_path before returning
        return handle_error_response(501, NULL, mime_type);
    }

    // Check if the path exists
    char* error_response = check_path(final_path);
    if (error_response != NULL) {
        free(final_path); // Free final_path before returning
        return error_response;
    }

    // Path is valid, handle the OK response
    char* ok_response = handle_ok_response(final_path);
    if (ok_response != NULL) {
        free(final_path); // Free final_path before returning
        return ok_response;
    }

    // If something went wrong, return a 500 Internal Server Error
    free(final_path); // Free final_path before returning
    return handle_error_response(500, NULL, mime_type);
}

// Function to handle client requests
int handle_client(void* arg) {
    int client_socket = *((int*)arg);
    char* request = read_request(client_socket);
    // Handle the request using the request_handler function
    char* response = request_handler(request);

    if (request == NULL) {
        close(client_socket);
        free(response);
        return -1;
    }

    // Send the response to the client
    if (response != NULL) {
        ssize_t bytes_sent = write(client_socket, response, strlen(response));
        if (bytes_sent < 0) {
            perror("write");
        }
        free(response);
    }

    // Clean up
    close(client_socket);
    free(request);
    return 0;
}

int main(int argc, char* argv[]){
    if(argc != 5){
        printf("Usage: server <port> <pool-size> <max-queue-size> <max-number-of-request>\n" );
        exit(1);
    }
    int port = atoi(argv[1]);
    int pool_size = atoi(argv[2]);
    int max_queue_size = atoi(argv[3]);
    int max_requests = atoi(argv[4]);

    if (port < 1 || port > 65535) {
        printf("Invalid port number. Port must be between 1 and 65535.\n");
        exit(1);
    }
    if (pool_size <= 0 || max_queue_size <= 0 || max_requests <= 0) {
        printf("Pool size, max queue size, and max number of requests must be positive integers.\n");
        exit(1);
    }

    threadpool* tp = create_threadpool(pool_size, max_queue_size);
    if (tp == NULL) {
       // fprintf(stderr, "Failed to create thread pool\n");
        return 1;
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        destroy_threadpool(tp);
        return 1;
    }

    // Bind the server socket to the port
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        destroy_threadpool(tp);
        return 1;
    }

    // Listen for incoming connections
    if (listen(server_socket, 5) < 0) {
        perror("ERR: Listen failed");
        close(server_socket);
        exit(1);
    }
    printf("Server is listening on port %d...\n", port);
    // Track the number of requests processed
    int request_count = 0;

    // Main server loop
    while (request_count < max_requests) {
        // Accept a new client connection
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }

        // Dispatch the client request to the thread pool
        dispatch(tp, handle_client, (void*)&client_socket);

        // Increment the request count
        request_count++;
    }

    // Shut down the server after processing the maximum number of requests
    printf("Processed %d requests. Shutting down...\n", max_requests);

    // Clean up
    close(server_socket);
    destroy_threadpool(tp);
    return 0;
}