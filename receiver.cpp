#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <cstring>
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <sstream>
#include <stdlib.h>
#include <stdint.h>
#include <queue>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <set>
#include <arpa/inet.h>
#include <algorithm>
#include <fcntl.h>
#include <poll.h>

#include "common.h"
#include "err.h"

std::string STATION_NAME = "Nienazwany Nadajnik"; // STATION_NAME to STATION_NAME nadajnika, ustawiana parametrem -n, domyślnie "NieSTATION_NAMEny Nadajnik"
std::string DISCOVER_ADDR = "255.255.255.255";

std::atomic<bool> start_sending = false;
uint64_t session_id = 0;
uint64_t first_byte_num = 0;
uint64_t left_byte_num = 0;
uint64_t right_byte_num = 0;
uint64_t current_session_id = 0;
uint64_t current_first_num = 0;

// uint16_t DATA_PORT = 20000 + (438753 % 10000); // port UDP używany do przesyłania danych, ustawiany parametrem -P nadajnika i odbiornika, domyślnie 20000 + (numer_albumu % 10000)
uint16_t UI_PORT = 10000 + (438753 % 10000);
uint16_t CTRL_PORT = 30000 + (438753 % 10000);
uint64_t B_SIZE = 65536 + 16; // rozmiar w bajtach bufora, ustawiany parametrem -b odbiornika, domyślnie 64kB (65536B)
uint64_t P_SIZE = 0; 
uint64_t R_TIME = 250;
uint64_t POM_SIZE;

#define CONNECTIONS 100
#define QUEUE_LENGTH 100

class Station{
public:
    std::string mcast_addr;
    uint16_t data_port;
    std::string name;
    uint16_t timer;

    Station(std::string mcast_addr, uint16_t data_port, std::string name){
        this->mcast_addr =  mcast_addr;
        this->data_port = data_port;
        this->name = name;
        this->timer = 4;
    }
};

bool compareStationsByName(const Station& station1, const Station& station2) {
    return station1.name < station2.name;
}

std::vector<Station> stations;
Station current_station("",0,"");

//https://stackoverflow.com/questions/4792449/c0x-has-no-semaphores-how-to-synchronize-threads
class semaphore {
    std::mutex mutex_;
    std::condition_variable condition_;
    uint64_t count_ = 0; // Initialized as locked.

public:
    void set(uint64_t number){
        count_ = number;
    }

    void release() {
        std::lock_guard<decltype(mutex_)> lock(mutex_);
        ++count_;
        condition_.notify_one();
    }

    void acquire() {
        std::unique_lock<decltype(mutex_)> lock(mutex_);
        while(!count_) // Handle spurious wake-ups.
            condition_.wait(lock);
        --count_;
    }

};

semaphore s_free, s_taken;

char * buffer;
char * pom;
std::mutex t_mutex;
std::set <u_int64_t>  taken;
std::mutex m_mutex;
std::set <u_int64_t>  missing;
std::set <u_int64_t>  pom_set;



void extract_flags(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "n:b:d:C:U")) != -1) {
        switch (opt) {   
            case 'n':
                STATION_NAME = optarg;
                break;
            case 'b':
                B_SIZE = string_to_int(optarg) + 16;
                break;
            case 'd':
                DISCOVER_ADDR = optarg;
                break;
            case 'C':
                CTRL_PORT = string_to_int(optarg);
                break;   
            case 'U':
                UI_PORT = string_to_int(optarg);
                break;
            case 'R':
                R_TIME = string_to_int(optarg);
                break;      
            default:
                break;
        }
    }
    if(check_addr(DISCOVER_ADDR)) {
        printf("DISCOVER_ADDR not viable\n");
        exit(1);
    }
    if(check_data_port(CTRL_PORT)){ 
        printf("CTRL_PORT not viable\n");
        exit(1);
    }
    if(check_data_port(CTRL_PORT)){
        printf("CTRL_PORT not viable\n");
        exit(1);
    }
    if(check_data_port(UI_PORT)){
        printf("UI_PORT not viable\n");
        exit(1);
    }
    if(check_bsize(B_SIZE)){
        printf("B_SIZE not viable\n");
        exit(1);
    }
    if(check_name(STATION_NAME)) {
        printf("Station Name not viable\n");
        exit(1);
    }
    if(check_rtime(R_TIME)){
        printf("R_TIME not viable\n");
        exit(1);
    }
}

void reset_sending(){
    current_session_id = session_id;
    start_sending = false;
    std::memset(buffer, 0, B_SIZE);
    current_first_num = first_byte_num;

    s_free.set((u_int64_t)B_SIZE/P_SIZE);   
    left_byte_num = first_byte_num;
    s_taken.set(0);

}


void printer(){
    while(true){
        if(start_sending.load()){
            t_mutex.lock();
            if(!taken.contains(left_byte_num)){
                t_mutex.unlock();
                reset_sending();
                continue;
            } 
            t_mutex.unlock();
            s_taken.acquire();
            std::cout.write(buffer + (left_byte_num%POM_SIZE), P_SIZE);
            s_free.release();

            left_byte_num += P_SIZE;
        }
    }
}

int socket_fd_2;

void re_send(){
    struct sockaddr_in send_address = get_send_address(DISCOVER_ADDR.data(), CTRL_PORT);
    socket_fd_2 = socket(AF_INET, SOCK_DGRAM, 0);

    int enable_broadcast = 1;
    setsockopt(socket_fd_2, SOL_SOCKET, SO_BROADCAST, &enable_broadcast, sizeof(enable_broadcast));

    while(true){
        const char* message = "ZERO_SEVEN_COME_IN";
        
        sendto(socket_fd_2, message, strlen(message), 0, (struct sockaddr*)&send_address, sizeof(send_address));
        std::this_thread::sleep_for(std::chrono::seconds(5));
        for (auto& station : stations) {
            if(station.timer > 0){
                station.timer -= 1;
            } else {
                stations.erase(std::remove_if(stations.begin(), stations.end(), [&](const Station& station) {
                    return station.name == station.name;
                }), stations.end());
            }
            
        }
    }
}

void get_stations(){
    char buffer[4096];
    struct sockaddr_in sender_address{};
    socklen_t sender_address_len = sizeof(sender_address);

    while(true){
        std::memset(buffer, 0, 4096);
        ssize_t received_length = recvfrom(socket_fd_2, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&sender_address, &sender_address_len);
        if(received_length > 0){
            std::stringstream ss;
            ss << buffer;
            std::string token;
            ss >> token;
            std::string secondToken, thirdToken;
            ss >> secondToken >> thirdToken;
            std::string remainingTokens;
            std::getline(ss, remainingTokens);
            remainingTokens = trim(remainingTokens);

            auto it = std::find_if(stations.begin(), stations.end(), [&](const Station& station) {
                return station.name == remainingTokens;
            });

            if (it == stations.end()) {
                uint16_t data_port = string_to_int(thirdToken);
                Station stacja(secondToken, data_port, remainingTokens);
                stations.push_back(stacja);
                std::sort(stations.begin(), stations.end(), compareStationsByName);
                if(current_station.name == "" || stacja.name == STATION_NAME){
                    current_station.name = stacja.name;
                    current_station.data_port = stacja.data_port;
                    current_station.mcast_addr = stacja.mcast_addr;
                }
            } else {
                it->timer++;
            }

        }

    }

}


void send_missing(){
    struct sockaddr_in send_address = get_send_address(DISCOVER_ADDR.data(), CTRL_PORT);

    while(true){
        std::this_thread::sleep_for(std::chrono::milliseconds(R_TIME));
        if(missing.size()){
            m_mutex.lock();
            
            std::swap(pom_set, missing);
            m_mutex.unlock();

            std::stringstream ss;
            ss << "LOUDER_PLEASE ";
            for (const auto& element : pom_set) {
                ss << element << ',';
            }
            std::string result = ss.str();
            result.pop_back();
            
            sendto(socket_fd_2, result.data(), strlen(result.data()), 0, (struct sockaddr*)&send_address, sizeof(send_address));
            missing.clear();
        }
    }
}

static bool finish = false;

std::vector<int> active_client_fds;

void ui_handler() {
    char buf[4096];

    struct pollfd poll_descriptors[CONNECTIONS];

    for (int i = 0; i < CONNECTIONS; ++i) {
        poll_descriptors[i].fd = -1;
        poll_descriptors[i].events = POLLIN;
        poll_descriptors[i].revents = 0;
    }
    size_t active_clients = 0;

    poll_descriptors[0].fd = open_socket();
    bind_socket(poll_descriptors[0].fd, UI_PORT);
    std::cerr << UI_PORT << '\n';
    start_listening(poll_descriptors[0].fd, QUEUE_LENGTH);

    do {
        for (int i = 0; i < CONNECTIONS; ++i) {
            poll_descriptors[i].revents = 0;
        }

        if (finish && poll_descriptors[0].fd >= 0) {
            CHECK_ERRNO(close(poll_descriptors[0].fd));
            poll_descriptors[0].fd = -1;
        }
        std::string response;
        int poll_status = poll(poll_descriptors, CONNECTIONS, -1);
        if (poll_status == -1) {
            if (errno == EINTR) 
                fprintf(stderr, "Interrupted system call\n");
            else    
                PRINT_ERRNO();
        } 
        else if (poll_status > 0) {
            if (!finish && (poll_descriptors[0].revents & POLLIN)) {
                int client_fd = accept_connection(poll_descriptors[0].fd, NULL);
                std::string res;
                std::string delimeter = "------------------------------------------------------------------------";
                std::ostringstream oss;
                oss << "\x1B[2J\x1B[H" << delimeter << "\n\r" << "SIK Radio\n\r" << delimeter << "\n\r";
                for(auto s: stations){
                    if(s.name == current_station.name){
                        oss << ">";
                    }
                    oss << s.name << "\n\r";
                }
                oss << delimeter << "\n\r";
                res = oss.str();

                unsigned char cmd[] = { 255, 251, 3, 255, 251, 1 };
                ssize_t xd = write(client_fd, cmd, sizeof(cmd));
                xd++;
                send(client_fd, res.c_str(), res.length(), 0);

                bool accepted = false;
                for (int i = 1; i < CONNECTIONS; ++i) {
                    if (poll_descriptors[i].fd == -1) {
                        fprintf(stderr, "Received new connection (%d)\n", i);

                        poll_descriptors[i].fd = client_fd;
                        poll_descriptors[i].events = POLLIN;
                        active_clients++;
                        accepted = true;
                        active_client_fds.push_back(client_fd);
                        break;
                    }
                }
                if (!accepted) {
                    CHECK_ERRNO(close(client_fd));
                    fprintf(stderr, "Too many clients\n");
                }
            }
            
            for (int i = 1; i < CONNECTIONS; ++i) {
                if (poll_descriptors[i].fd != -1 && (poll_descriptors[i].revents & (POLLIN | POLLERR))) {
                    ssize_t received_bytes = read(poll_descriptors[i].fd, buf, 4096);
                    if (received_bytes > 0) {
                        if (strncmp(buf, "\x1B[A", 3) == 0) {
                            
                            std::string delimeter = "------------------------------------------------------------------------";
                            std::ostringstream oss;
                            oss << "\x1B[2J\x1B[H" << delimeter << "\n\r" << "SIK Radio\n\r" << delimeter << "\n\r";
                            Station* previousStation = nullptr;
                            for (auto it = stations.begin(); it != stations.end(); ++it) {
                                if (it->name == current_station.name) {
                                    if (it != stations.begin()) {
                                        previousStation = &(*(it - 1));
                                    }
                                    break;
                                }
                            }
                            if (previousStation != nullptr) {
                                current_station.name = previousStation->name;
                                current_station.data_port = previousStation->data_port;
                                current_station.mcast_addr = previousStation->mcast_addr;
                            } 
                            for(auto s: stations){
                                if(s.name == current_station.name){
                                    oss << ">";
                                }
                                oss << s.name << "\n\r";
                            }
                            oss << delimeter << "\n\r";
                            response = oss.str();
                        } else if (strncmp(buf, "\x1B[B", 3) == 0) {
                            
                            std::string delimeter = "------------------------------------------------------------------------";
                            std::ostringstream oss;
                            oss << "\x1B[2J\x1B[H" << delimeter << "\n\r" << "SIK Radio\n\r" << delimeter << "\n\r";
                            Station* nextStation = nullptr;
                            for (auto it = stations.begin(); it != stations.end(); ++it) {
                                if (it->name == current_station.name) {
                                    if (it != std::prev(stations.end())) {
                                        nextStation = &(*(it + 1));
                                    }
                                    break;
                                }
                            }
                            if (nextStation != nullptr) {
                                current_station.name = nextStation->name;
                                current_station.data_port = nextStation->data_port;
                                current_station.mcast_addr = nextStation->mcast_addr;
                            } 
                            for(auto s: stations){
                                if(s.name == current_station.name){
                                    oss << ">";
                                }
                                oss << s.name << "\n\r";
                            }
                            oss << delimeter << "\n\r";
                            response = oss.str();
                        }
                    }
                }
            }

            for (int i = 1; i < CONNECTIONS; ++i) {
                send(poll_descriptors[i].fd, response.c_str(), response.length(), 0);
            }

            
            std::vector<int> disconnected_fds;
            for (const auto& client_fd : active_client_fds) {
                struct pollfd client_poll_fd;
                client_poll_fd.fd = client_fd;
                client_poll_fd.events = POLLIN;
                client_poll_fd.revents = 0;

                int poll_status = poll(&client_poll_fd, 1, 0);
                if (poll_status == -1) {
                    // Handle error
                } else if (poll_status > 0) {
                    if (client_poll_fd.revents & POLLHUP) {
                        fprintf(stderr, "Client disconnected\n");
                        disconnected_fds.push_back(client_fd);
                    }
                }
            }

            // Remove disconnected clients from the active clients list
            for (const auto& disconnected_fd : disconnected_fds) {
                auto it = std::find(active_client_fds.begin(), active_client_fds.end(), disconnected_fd);
                if (it != active_client_fds.end()) {
                    active_client_fds.erase(it);
                }
            }

            // Reset the disconnected client's poll descriptor
            for (int i = 1; i < CONNECTIONS; ++i) {
                if (poll_descriptors[i].fd != -1 && std::find(disconnected_fds.begin(), disconnected_fds.end(), poll_descriptors[i].fd) != disconnected_fds.end()) {
                    poll_descriptors[i].fd = -1;
                    poll_descriptors[i].events = POLLIN;
                    active_clients--;
                }
            }
        }
    } while (!finish);

    // Close all active client connections
    for (const auto& client_fd : active_client_fds) {
        CHECK_ERRNO(close(client_fd));
    }
}


int bind_s(uint64_t port, char * adress, struct sockaddr_in &server_address ){
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_fd <  0){
        std::cerr << strerror(errno) << '\n';
        close(socket_fd);
        exit(1);
    }

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = inet_addr(adress);
    server_address.sin_port = htons(port);

    if(bind(socket_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0){
        std::cerr << strerror(errno) << '\n';
        close(socket_fd);
        exit(1);
    }

    return socket_fd;
}

int main(int argc, char *argv[]){
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);
    
    extract_flags(argc, argv);  

    buffer = (char*)malloc(B_SIZE);  
    pom = (char*)malloc(B_SIZE);  

    std::thread t(printer);
    std::thread t2(re_send);
    std::thread t3(get_stations);
    std::thread t4(send_missing);
    std::thread t5(ui_handler);
    t.detach();
    t2.detach();
    t3.detach();
    t4.detach();
    t5.detach();

    while(current_station.name == ""){sleep(0);}

    struct sockaddr_in servadrr{};
    socklen_t len = sizeof(servadrr);
    // std::cerr << current_station->data_port << " " << current_station->mcast_addr << '\n';
    int socket_fd = bind_s(current_station.data_port,(current_station.mcast_addr).data(), servadrr);
    // std::cerr << socket_fd << '\n';
    struct sockaddr_in receive_address;

    while(true){

        if(servadrr.sin_addr.s_addr != inet_addr((current_station.mcast_addr).data())){
            close(socket_fd);
            socket_fd = bind_s(current_station.data_port,(current_station.mcast_addr).data(), servadrr);
        }

        std::memset(pom, 0, B_SIZE);
        P_SIZE = recvfrom(socket_fd, pom, B_SIZE, 0, (struct sockaddr *) &receive_address, &len) - 16;
        POM_SIZE = B_SIZE - (B_SIZE%P_SIZE);

        memcpy(&session_id, pom, 8);    
        memcpy(&first_byte_num, pom + 8, 8);
        session_id = be64toh(session_id);
        first_byte_num = be64toh(first_byte_num);

        if(current_session_id != session_id){
            t_mutex.lock();
            taken.clear();
            t_mutex.unlock();
            m_mutex.lock();
            missing.clear();
            m_mutex.unlock();
            reset_sending();
        }

        t_mutex.lock();
        taken.insert(first_byte_num);   
        t_mutex.unlock();

        if(first_byte_num >= current_first_num + (3 * POM_SIZE / 4)){
            start_sending = true;
        }

        right_byte_num = first_byte_num % POM_SIZE;

        if(first_byte_num > left_byte_num){
            s_free.acquire();
            memcpy(buffer + right_byte_num, pom + 16, P_SIZE);
            s_taken.release();
        }

        for(uint64_t i = left_byte_num; i < first_byte_num;i += P_SIZE){
            if(!taken.contains(i)){
                missing.insert(i);
            }
        }

    }

    close(socket_fd);

    return 0;
}
