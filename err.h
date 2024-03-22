#ifndef _ERR_
#define _ERR_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <stdbool.h>

// Evaluate `x`: if non-zero, describe it as a standard error code and exit with an error.
#define CHECK(x)                                                          \
    do {                                                                  \
        int err = (x);                                                    \
        if (err != 0) {                                                   \
            fprintf(stderr, "Error: %s returned %d in %s at %s:%d\n%s\n", \
                #x, err, __func__, __FILE__, __LINE__, strerror(err));    \
            exit(EXIT_FAILURE);                                           \
        }                                                                 \
    } while (0)

// Evaluate `x`: if false, print an error message and exit with an error.
#define ENSURE(x)                                                         \
    do {                                                                  \
        bool result = (x);                                                \
        if (!result) {                                                    \
            fprintf(stderr, "Error: %s was false in %s at %s:%d\n",       \
                #x, __func__, __FILE__, __LINE__);                        \
            exit(EXIT_FAILURE);                                           \
        }                                                                 \
    } while (0)

// Check if errno is non-zero, and if so, print an error message and exit with an error.
#define PRINT_ERRNO()                                                  \
    do {                                                               \
        if (errno != 0) {                                              \
            fprintf(stderr, "Error: errno %d in %s at %s:%d\n%s\n",    \
              errno, __func__, __FILE__, __LINE__, strerror(errno));   \
            exit(EXIT_FAILURE);                                        \
        }                                                              \
    } while (0)


// Set `errno` to 0 and evaluate `x`. If `errno` changed, describe it and exit.
#define CHECK_ERRNO(x)                                                             \
    do {                                                                           \
        errno = 0;                                                                 \
        (void) (x);                                                                \
        PRINT_ERRNO();                                                             \
    } while (0)

// Note: the while loop above wraps the statements so that the macro can be used with a semicolon
// for example: if (a) CHECK(x); else CHECK(y);


// Print an error message and exit with an error.
void fatal(const char *fmt, ...) {
    va_list fmt_args;

    fprintf(stderr, "Error: ");
    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

inline static void set_port_reuse(int socket_fd) {
    int option_value = 1;
    CHECK_ERRNO(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &option_value, sizeof(option_value)));
}

extern struct sockaddr_in get_send_address(const char* host, uint16_t port) {
    struct addrinfo hints;
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *address_result;
    int status = getaddrinfo(host, NULL, &hints, &address_result);
    if (status != 0) {
        std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
        exit(1);
    }

    struct sockaddr_in send_address;
    // memset(&send_address, 0, sizeof(struct sockaddr_in));
    send_address.sin_family = AF_INET; // IPv4
    send_address.sin_addr.s_addr = INADDR_ANY;
    // send_address.sin_addr.s_addr = ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr; // IP address
    send_address.sin_port = htons(port); // port from the command line

    freeaddrinfo(address_result);

    return send_address;
}

extern int bind_socket2(uint16_t port) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0); // creating IPv4 UDP socket
    ENSURE(socket_fd >= 0);
    // after socket() call; we should close(sock) on any execution path;
    set_port_reuse(socket_fd);
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(port);

    // bind the socket to a concrete address
    CHECK_ERRNO(bind(socket_fd, (struct sockaddr *) &server_address,
                     (socklen_t) sizeof(server_address)));

    return socket_fd;
}

struct sockaddr_in get_send_address(char *host, uint16_t port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *address_result;
    CHECK(getaddrinfo(host, NULL, &hints, &address_result));

    struct sockaddr_in send_address;
    send_address.sin_family = AF_INET; // IPv4
    send_address.sin_addr.s_addr =
            ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr; // IP address
    send_address.sin_port = htons(port); // port from the command line

    freeaddrinfo(address_result);

    return send_address;
}

extern std::string trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n");
    auto end = str.find_last_not_of(" \t\r\n");
    if (start == std::string::npos || end == std::string::npos) {
        return "";
    }
    return str.substr(start, end - start + 1);
}




extern uint64_t string_to_int(std::string str) {
    std::reverse(str.begin(), str.end());
    uint64_t res = 0;
    uint64_t multi = 1;
    for(auto c : str) {
        if(c >= '0' && c <= '9'){
            res += (c - '0')*multi;
        } else { 
            return 0;
        }
        multi *= 10;
    }
    return res;
}    

extern bool check_addr(std::string addr) {
    int counter = 0;
    std::string part = "";
    std::vector<std::string> parts;
    for(auto a : addr) {
        if(a == '.') {
            if(counter == 0 || counter > 4){
                std::cout << counter << " " << a << '\n';
                return true;
            }
            parts.push_back(part);
            part = "";
            counter = 0;
        } else if((a >= '0' && a <= '9') == false) {
            return true; 
        } else { 
            part += a;
            counter++;
        } 
    }
    if(part != "") {
        parts.push_back(part);
    }
    if(parts.size() > 4) {
        return true;
    }
    for(auto str : parts) {
        if(std::stoi(str) < 0 || std::stoi(str) > 255){
            return true;
        }
    }
    return false;
}

extern bool check_fsize(int f_size){
    if(f_size < (int)0 || (long unsigned int)f_size > UINT64_MAX){
        return true;
    }
    return false;
}

extern bool check_data_port(int data_port){
    if(data_port < (int)0 || (long unsigned int)data_port > UINT16_MAX){
        return true;
    }
    return false;
}

extern bool check_name(std::string nazwa){
    if(nazwa.length() == 0){return true;}
    for(size_t i = 0;i < nazwa.length();i++){
        if(i == 0 && nazwa[i] == ' '){
            return true;
        }
        if(i == nazwa.length()-1 and nazwa[i] == ' '){
            return true;
        }
        // if(int(nazwa[i]) < 32 or int(nazwa[i] > 127)){
        //     return true;
        // }
    }
    return false;
}

extern bool check_rtime(uint64_t r_time){
    if(r_time < 1){
        return true;
    }
    return false;
}

extern bool check_psize(uint64_t p_size){
    if(p_size < 1){
        return true;
    }
    if(p_size > 65507){
        return true;
    }
    return false;
}

extern bool check_bsize(uint64_t b_size){
    if(b_size < 1){
        return true;
    }
    if(b_size > UINT64_MAX){
        return true;
    }
    return false;
}





#endif