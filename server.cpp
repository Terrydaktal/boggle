#include <sys/socket.h> // For socket functions
#include <netinet/in.h> // For sockaddr_in
#include <cstdlib> // For exit() and EXIT_FAILURE
#include <iostream> // For cout
#include <unistd.h> // For read

int main() {
  // Create a socket (IPv4, TCP)
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    std::cout << "Failed to create socket. errno: " << errno << std::endl;
    exit(EXIT_FAILURE);
  }

  // Listen to port 9999 on any address
  sockaddr_in sockaddr;
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_addr.s_addr = INADDR_ANY;
  sockaddr.sin_port = htons(80); // htons is necessary to convert a number to
                                   // network byte order

  
    //listen to all destination addresses on the incoming packet and on port 9999
   //it sets _our_ address , and all packets destined to to it will be accepted
	//and all packets sent from us will have this address and port
	//effectively it means that it accepts all communications to port 9999 regardless of address
	//and sends all packets from the appropriate outgoing address and port 9999

	if (bind(sockfd, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) < 0) {
		std::cout << "Failed to bind to port 9999. errno: " << errno << std::endl;
		exit(EXIT_FAILURE);
	}


  std::cout << "listening " << errno << std::endl;
  // Start listening. Hold at most 10 connections in the queue
  if (listen(sockfd, 10) < 0) {
    std::cout << "Failed to listen on socket. errno: " << errno << std::endl;
    exit(EXIT_FAILURE);
  }
  std::cout << "done " << errno << std::endl;
  // Grab a connection from the queue
  auto addrlen = sizeof(sockaddr);
  int connection = accept(sockfd, (struct sockaddr*)&sockaddr, (socklen_t*)&addrlen);
  if (connection < 0) {
    std::cout << "Failed to grab connection. errno: " << errno << std::endl;
    exit(EXIT_FAILURE);
  }

  // Read from the connection
  char buffer[100];
  auto bytesRead = read(connection, buffer, 100);
  std::cout << "The message was: " << buffer;

  // Send a message to the connection
  std::string response = "Good talking to you\n";
  send(connection, response.c_str(), response.size(), 0);

  // Close the connections
  close(connection);
  close(sockfd);
}
