#include <stdio.h> /* printf, sprintf */
#include <stdlib.h> /* exit, atoi, malloc, free */
#include <unistd.h> /* read, write, close */
#include <string>
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h> /* struct hostent, gethostbyname */

#include <unordered_map>

void error(const char *msg) { perror(msg); exit(0); }

using std::string;

struct HTTPRequest {
    string version;

    string method;
    string path;
    std::unordered_map<string, string> params;

    std::unordered_map<string, string> headers;

    string body;
};

struct HTTPResponse {
    string version;

    string status;
    string message;

    std::unordered_map<string, string> headers;

    string body;
};

void printResponse(HTTPResponse response) {
    printf("\nResponse:\n");
    printf("%s %s %s\n", response.version.c_str(), response.status.c_str(), response.message.c_str());
    for (auto header : response.headers) {
        printf("%s: %s\n", header.first.c_str(), header.second.c_str());
    }
    printf("\n%s\n", response.body.c_str());
}

class HTTP {
    public:
        HTTP(string host, int port) {
            this->host = host;
            this->port = port;

            struct hostent *server;
            struct sockaddr_in serv_addr;

            this->sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (this->sockfd < 0) error("ERROR opening socket");

            /* lookup the ip address */
            server = gethostbyname(this->host.c_str());
            if (server == NULL) error("ERROR no such host");

            memset(&serv_addr,0,sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(this->port);
            memcpy(&serv_addr.sin_addr.s_addr,server->h_addr,server->h_length);

            /* connect the socket */
            if (connect(this->sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
                error("ERROR connecting");
        }

        ~HTTP() {
            close(this->sockfd);
        }

        HTTPResponse get(string path, std::unordered_map<string, string> params, std::unordered_map<string, string> headers) {
            HTTPRequest request;
            request.version = "HTTP/1.1";
            request.method = "GET";
            request.path = path;
            request.params = params;
            request.headers = headers;

            return this->request(request);
        }

        HTTPResponse get(string path, std::unordered_map<string, string> params) {
            return this->get(path, params, std::unordered_map<string, string>());
        }
        HTTPResponse get(string path) {
            return this->get(path, std::unordered_map<string, string>());
        }

        HTTPResponse post(string path, std::unordered_map<string, string> params, std::unordered_map<string, string> headers, string body) {
            HTTPRequest request;
            request.version = "HTTP/1.1";
            request.method = "POST";
            request.path = path;
            request.params = params;
            request.headers = headers;
            request.body = body;

            return this->request(request);
        }

    private:
        string host;
        int port;
        int sockfd;

        string serialize(HTTPRequest request) {
            string message;

            if (request.method == "GET") {
                message += request.method + " " + request.path;
                if (request.params.size() > 0) {
                    message += "?";
                    for (auto param : request.params) {
                        message += param.first + "=" + param.second + "&";
                    }
                    message.pop_back();
                }
                message += " " + request.version + "\r\n";
            } else {
                message += request.method + " " + request.path + " " + request.version + "\r\n";
            }

            for (auto header : request.headers) {
                string headerName = header.first;
                for (auto &c : headerName) {
                    c = toupper(c);
                }

                message += headerName + ": " + header.second + "\r\n";
            }

            message += "Host: " + this->host + "\r\n";
            message += "Connection: close\r\n";

            if (request.method == "POST") {
                message += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
            }

            message += "\r\n";

            if (request.method == "POST") {
                message += request.body;
            }

            return message;
        }

        HTTPResponse deserialize(string message) {
            HTTPResponse response;

            response.version = message.substr(0, message.find(" "));
            message = message.substr(message.find(" ")+1);

            response.status = message.substr(0, message.find(" "));
            message = message.substr(message.find(" ")+1);

            response.message = message.substr(0, message.find("\r\n"));
            message = message.substr(message.find("\r\n")+2);

            while (message.find("\r\n") != 0) {
                string header = message.substr(0, message.find(": "));
                message = message.substr(message.find(": ")+2);
                string value = message.substr(0, message.find("\r\n"));
                message = message.substr(message.find("\r\n")+2);

                // Lowercase header
                for (auto &c : header) {
                    c = tolower(c);
                }

                response.headers[header] = value;
            }

            message = message.substr(2);

            // If transfer encoding is chunked, we need to handle that here by removing the chunked encoding
            if (response.headers.find("transfer-encoding") != response.headers.end() && response.headers["transfer-encoding"] == "chunked") {
                string newBody = "";
                while (message.size() > 0) {
                    int chunkSize = std::stoi(message.substr(0, message.find("\r\n")), 0, 16);
                    message = message.substr(message.find("\r\n")+2);

                    fprintf(stderr, "Chunk size: %d%s\n", chunkSize, message.substr(0, chunkSize).c_str());

                    newBody += message.substr(0, chunkSize);
                    message = message.substr(chunkSize+2);
                }

                response.body = newBody;
            } else {
                response.body = message;
            }

            return response;
        }

        /**
         * This is currently inefficient and easy to break.
         * 
         * The buffer size is fixed at 500000 bytes.
         * We completely ignore the Content-Length header (or, chunked encoding if not present) to determine the size of the response and dynamically allocate memory.
         */
        HTTPResponse request(HTTPRequest request) {
            HTTPResponse httpResponse;

            // Send request to server
            string message = this->serialize(request);

            // printf("Request:\n%s\n", message.c_str());

            int sent, bytes, total;
            total = message.size();
            sent = 0;
            do {
                bytes = write(this->sockfd,message.c_str()+sent,total-sent);
                if (bytes < 0)
                    error("ERROR writing message to socket");
                if (bytes == 0)
                    break;
                sent+=bytes;
            } while (sent < total);

            // Receive response from server
            char response[500000];
            memset(response, 0, sizeof(response));
            
            total = sizeof(response)-1;
            int received = 0;

            do {
                bytes = read(this->sockfd,response+received,total-received);
                if (bytes < 0)
                    error("ERROR reading response from socket");
                if (bytes == 0)
                    break;
                received += bytes;
            } while (received < total);


            /*
            * if the number of received bytes is the total size of the
            * array then we have run out of space to store the response
            * and it hasn't all arrived yet - so that's a bad thing
            */
            if (received == total){
                error("ERROR storing complete response from socket");
            }
                

            message = response;

            httpResponse = this->deserialize(message);

            return httpResponse;
        }
};