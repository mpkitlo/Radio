#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sstream>
#include <stdlib.h>
#include <algorithm>
#include <cstring>
#include <bitset>
#include <thread>
#include <stdint.h>
#include <queue>
#include <mutex>
#include <iostream>
#include <ctime>
#include <string>
#include <arpa/inet.h>
#include <fcntl.h>

#include "err.h"

std::string STATION_NAME = "Nienazwany Nadajnik"; // STATION_NAME to STATION_NAME nadajnika, ustawiana parametrem -n, domyślnie "NieSTATION_NAMEny Nadajnik"
std::string MCAST_ADDR ; // adres odbiornika, ustawiany obowiązkowym parametrem -a nadajnika

uint16_t DATA_PORT = 20000 + (438753 % 10000); // port UDP używany do przesyłania danych, ustawiany parametrem -P nadajnika i odbiornika, domyślnie 20000 + (numer_albumu % 10000)
uint16_t CTRL_PORT = 30000 + (438753 % 10000);
uint64_t P_SIZE = 512; // rozmiar w bajtach pola audio_data paczki, ustawiany parametrem -p nadajnika, domyślnie 512B
uint64_t F_SIZE = 128000;
uint64_t R_TIME = 250;

class Package{
public:
    char * data;
    uint64_t package_number;
    Package(char * data, uint64_t package_number){
        this->data = data;
        this->package_number = package_number;
    }
    Package() = default;
    ~Package() = default;
};

std::mutex resend_m;
Package * RESEND;

std::vector<uint64_t> rexmit_packages;
std::mutex rexmit_m;
std::vector<uint64_t> pom;
int socket_fd;

uint64_t session_id;
uint64_t first_byte_num;
struct sockaddr_in servadrr;

void extract_flags(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "n:a:P:p:C:f:R")) != -1) {
        switch (opt) {
            case 'n':
                STATION_NAME = optarg;
                break;
            case 'a':
                MCAST_ADDR  = optarg;
                break;
            case 'P':
                DATA_PORT = string_to_int(optarg);
                break;
            case 'p':
                P_SIZE = string_to_int(optarg);
                break;
            case 'C':
                CTRL_PORT = string_to_int(optarg);
                break;
            case 'f':
                F_SIZE = string_to_int(optarg);
                break;   
            case 'R':
                R_TIME = string_to_int(optarg);
                break;       
            default:
                break;
        }
    }
    if (MCAST_ADDR.empty()) {
        printf("-a flag is required!\n");
        exit(1);
    }
    if(check_addr(MCAST_ADDR)) {
        printf("MCAST_ADDR not viable\n");
        exit(1);
    }
    if(check_data_port(DATA_PORT)){
        printf("DATA_PORT not viable\n");
        exit(1);
    }
    if(check_data_port(CTRL_PORT)){
        printf("CTRL_PORT not viable\n");
        exit(1);
    }
    if(check_psize(P_SIZE)){
        printf("P_SIZE not viable\n");
        exit(1);
    }
    if(check_fsize(F_SIZE)){
        printf("F_SIZE not viable\n");
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
    F_SIZE -= F_SIZE%P_SIZE;
}

void waitForMsg(){
    int socket_fd = bind_socket2(CTRL_PORT);

    std::string LOOKUP = "ZERO_SEVEN_COME_IN";
    std::string REXMIT = "LOUDER_PLEASE";

    while(true){
        char buffer[4096];
        std::memset(buffer, 0, 4096);
        struct sockaddr_in client_address{};
        socklen_t client_address_len = sizeof(client_address);
        recvfrom(socket_fd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&client_address, &client_address_len);
        
        std::stringstream ss;
        ss << buffer;
        std::string call = ss.str();

        std::size_t lookup_call = call.find(LOOKUP);
        std::size_t rexmit_call = call.find(REXMIT);

        if (lookup_call != std::string::npos) {
            std::string response = "BOREWICZ_HERE " + MCAST_ADDR + " " + std::to_string(DATA_PORT) + " " + STATION_NAME + "\n";
            sendto(socket_fd, response.data(), strlen(response.data()), 0, (struct sockaddr*)&client_address, client_address_len);
        }
        if (rexmit_call != std::string::npos) {
            std::stringstream ss;
            ss << buffer;
            std::string token;
            ss >> token;
            std::string number;
            while (std::getline(ss, number, ',')) {
                uint64_t value = std::stoi(number);
                // std::cerr << value << '\n';
                rexmit_m.lock();
                pom.push_back(value);
                rexmit_m.unlock();
            }
        }
    }
}

void rexmit(){
    char * buffer = (char*)malloc(P_SIZE + 16);
    while(true){
        std::this_thread::sleep_for(std::chrono::milliseconds(R_TIME));
        if(pom.size() > 0){
            rexmit_m.lock();
            std::swap(rexmit_packages, pom);
            rexmit_m.unlock();
            std::sort(rexmit_packages.begin(), rexmit_packages.end());
            for(size_t i = 0;i < rexmit_packages.size();i++){
                resend_m.lock();
                uint64_t number = RESEND[(rexmit_packages[i]/P_SIZE)%F_SIZE].package_number;
                char * data = RESEND[(rexmit_packages[i]/P_SIZE)%F_SIZE].data;
                resend_m.unlock();
                if(number == rexmit_packages[i]){
                    std::memset(buffer, 0, P_SIZE);
                    uint64_t pom1 = htobe64(session_id);
                    uint64_t pom2 = htobe64(number);
                    memcpy(buffer, &pom1, 8);
                    memcpy(buffer + 8, &pom2, 8);
                    memcpy(buffer + 16, &data, P_SIZE);
                    sendto(socket_fd, buffer, P_SIZE + 16, 0, (const struct sockaddr *) &servadrr ,(socklen_t) sizeof(servadrr));
                }
            }
            rexmit_packages.clear();
        }
    }
}


int main(int argc, char *argv[]){
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    extract_flags(argc, argv);

    std::thread t(waitForMsg);
    std::thread t2(rexmit);
    t.detach();
    t2.detach();

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if(socket_fd == -1){
        std::cerr << strerror(errno) << '\n';   
        close(socket_fd);
        exit(1);
    }

    servadrr = get_send_address(MCAST_ADDR.data(), DATA_PORT);

    session_id = time(NULL);
    first_byte_num = 0;

    char * buffer = (char*)malloc(P_SIZE + 16);
    RESEND = new Package[F_SIZE/P_SIZE];

    while(true){
        std::memset(buffer, 0, P_SIZE);

        uint64_t pom1 = htobe64(session_id);
        uint64_t pom2 = htobe64(first_byte_num);

        memcpy(buffer, &pom1, 8);
        memcpy(buffer + 8, &pom2, 8);

        std::cin.read(buffer + 16, P_SIZE);

        char * pom3 = (char *)malloc(P_SIZE);
        memcpy(pom3, buffer + 16, P_SIZE);        

        if (std::cin.fail()){
            close(socket_fd); 
            exit(0);
        }

        if(sendto(socket_fd, buffer, P_SIZE + 16, 0, (const struct sockaddr *) &servadrr ,(socklen_t) sizeof(servadrr)) == -1){
            std::cerr << strerror(errno) << '\n';
            close(socket_fd);
            exit(1);
        }    

        Package paczka(pom3, first_byte_num);
        resend_m.lock();
        RESEND[(first_byte_num/P_SIZE)%F_SIZE] = paczka;
        resend_m.unlock();

        first_byte_num += P_SIZE;
    }

    close(socket_fd);   
    return 0;
}
