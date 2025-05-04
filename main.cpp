#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

int main() {
   const char* hostname = "127.0.0.1"; 
   int port = 8333;

   int sock = socket(AF_INET, SOCK_STREAM, 0);
   if (sock < 0) {
    perror("socket");
   }

   sockaddr_in addr = {};
   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);
   inet_pton(AF_INET, hostname, &addr.sin_addr);

   std::cout << "Connecting to " << hostname << ":" << port << "..." << std::endl;

   if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("connect");
    close(sock);
    return 1;
   }

   std::cout << "Connected." << std::endl;

   // Send some junk to elicit any reaction
   const char* msg = "ffff";
   send(sock, msg, strlen(msg), 0);

   char buffer[1024];
   int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
   if (received > 0) {
    buffer[received] = 0;
    std::cout << "Received: " << buffer << std::endl;
   } else {
    std::cout << "No response." << std::endl;
   }

   close(sock);
   return 0;
}