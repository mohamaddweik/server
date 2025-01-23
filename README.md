HTTP Server Application

Authored by Mohamad Dweik

==Description==
This project implements a simple HTTP server in C that handles GET requests, serves files, generates directory listings, and manages HTTP error responses. The server is multi-threaded, using a thread pool to handle multiple client connections concurrently.

==Program Database==
1)HTTP Request Handling:

Parses incoming HTTP requests.

Validates the request method (only GET is supported).

Handles file requests and directory listings.

2)File and Directory Handling:

Serves files with appropriate MIME types.

Generates directory listings in HTML format for directories.

Handles redirections for directories missing trailing slashes.

3)Error Handling:

Returns appropriate HTTP error responses (e.g., 400 Bad Request, 404 Not Found, 500 Internal Server Error).

4)Thread Pool:

Uses a thread pool to manage multiple client connections efficiently.

Implements a work queue for dispatching tasks to worker threads.

==Functions==
Main Functions
1)create_threadpool: Initializes the thread pool with a specified number of threads and queue size.

2)dispatch: Adds a task to the thread pool's work queue.

3)do_work: Worker thread function that processes tasks from the queue.

4)destroy_threadpool: Shuts down the thread pool and cleans up resources.

5)handle_client: Handles client requests by reading the request, generating a response, and sending it back.

6)request_handler: Parses the HTTP request and generates the appropriate response.

7)handle_file_response: Serves files with the correct MIME type and content length.

8)generate_directory_listing: Generates an HTML listing of directory contents.

9)handle_error_response: Generates HTTP error responses with appropriate status codes and messages.

Helper Functions
1)get_mime_type: Determines the MIME type based on the file extension.

2)getFullPath: Constructs the full path for a given relative path.

3)ends_with_slash: Checks if a path ends with a slash.

4)read_request: Reads the first line of an HTTP request from a client socket.

==Program Files==
server.c: Contains the main server logic, including request handling, file serving, and error management.

threadpool.c: Implements the thread pool for handling multiple client connections concurrently.

threadpool.h: Header file defining the thread pool structures and functions.

==How to Compile==
To compile the server, use the following command:

gcc -Wall -o server server.c threadpool.c -lpthread

==Input==
The server accepts the following command-line arguments:

./server <port> <pool-size> <max-queue-size> <max-number-of-requests>

<port>: The port number on which the server will listen (must be between 1 and 65535).

<pool-size>: The number of threads in the thread pool (must be a positive integer).

<max-queue-size>: The maximum size of the work queue (must be a positive integer).

<max-number-of-requests>: The maximum number of requests the server will handle before shutting down (must be a positive integer).

==Output==
The server listens for incoming HTTP GET requests on the specified port.

For valid requests, it serves files or generates directory listings.

For invalid requests, it returns appropriate HTTP error responses.

The server shuts down after processing the specified number of requests.
