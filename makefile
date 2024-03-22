.PHONY: all clean

all: sikradio-sender sikradio-receiver


sikradio-sender: sender.cpp
	g++ -pthread -Wall -Wextra -std=c++20 -O2 sender.cpp -o sikradio-sender 

sikradio-receiver: receiver.cpp
	g++ -pthread -Wall -Wextra -std=c++20 -O2 receiver.cpp -o sikradio-receiver

clean:
	rm -rf sikradio-sender sikradio-receiver


