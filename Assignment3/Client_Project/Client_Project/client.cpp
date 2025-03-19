/* Start Header
*****************************************************************/
/*!
\file client.cpp
\author Goh Pin Kai, pinkai.g, 2301388
\par pinkai.g\@digipen.edu
\date  2024-02-20
\brief This program implements a TCP client using Winsock for communication with a server.
       It supports sending commands such as echo requests, retrieving a list of connected users, and disconnecting from the server.
       The client reads server connection details from a file or user input and establishes a connection using IPv4 and TCP.
       It runs two separate threads: one for handling user input and sending commands, and another for receiving and processing server responses.
       The client properly manages sockets and cleans up resources upon termination.

Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows.h"    // Entire Win32 API...
#include "ws2tcpip.h"    // getaddrinfo()
#include <thread>

#pragma comment(lib, "ws2_32.lib")

#include <iostream>     // cout, cerr
#include <string>       // string
#include <sstream>      // stringstream
#include <iomanip>      // string formatting
#include <vector>
#include <fstream>
#include <thread>
std::size_t limit = 805000000;
enum CMDID {
    UNKNOWN = (unsigned char)0x0,
    REQ_QUIT = (unsigned char)0x1,
    REQ_ECHO = (unsigned char)0x2,
    RSP_ECHO = (unsigned char)0x3,
    REQ_LISTUSERS = (unsigned char)0x4,
    RSP_LISTUSERS = (unsigned char)0x5,
    CMD_TEST = (unsigned char)0x20,
    ECHO_ERROR = (unsigned char)0x30
};
/**
 * @brief Parses the echo command input by the user.
 *
 * This function takes a user input string that represents an echo command, parses it,
 * and extracts the IP address, port, and message to be echoed. The command format is expected
 * to follow '/e IP:Port Message'.
 *
 * @param command The command string input by the user.
 * @param ip Reference to a string where the extracted IP address will be stored.
 * @param port Reference to a uint16_t where the extracted port number will be stored.
 * @param message Reference to a string where the extracted message will be stored.
 * @return true if the command was successfully parsed; false otherwise.
 */
bool parseEchoCommand(const std::string& command, std::string& ip, uint16_t& port, std::string& message) {
    std::istringstream iss(command);
    std::string cmd;
    std::getline(iss, cmd, ' '); // Extract the command '/e'

    if (cmd != "/e") {
        return false; // Incorrect command format
    }

    std::string ip_port;
    std::getline(iss, ip_port, ' '); // Extract the 'IP:Port'

    size_t colon_pos = ip_port.find(':');
    if (colon_pos == std::string::npos) {
        return false; // Missing colon
    }

    ip = ip_port.substr(0, colon_pos); // Extract IP
    std::string port_str = ip_port.substr(colon_pos + 1); // Extract Port string

    try {
        port = static_cast<uint16_t>(std::stoul(port_str)); // Convert port string to number
    }
    catch (const std::invalid_argument& ia) {
        return false; // Invalid port number
    }
    catch (const std::out_of_range& oor) {
        return false; // Port number out of range
    }

    // The rest of the input is the message
    std::getline(iss, message); // Extract the message
    return true;
}

/**
 * @brief Entry point of the client application.
 *
 * Initializes the Winsock API, establishes a connection to the server at the specified IP address
 * and port number, and handles user input for sending commands to the server. It supports sending echo requests,
 * requesting a list of connected users, and disconnecting from the server. The client utilizes separate threads
 * for sending and receiving data to handle asynchronous communication with the server.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return int Returns 0 on successful execution, or an error code on failure.
 */
int main(int argc, char* argv[]) {
    std::string serverIP;
    uint16_t port;

    if (argc == 2) {
        // Command-line argument provided, use it as the input file
        const char* inputFile = argv[1];

        // Open the input file
        std::ifstream fileStream(inputFile);
        if (!fileStream.is_open()) {
            std::cerr << "Failed to open input file: " << inputFile << std::endl;
            WSACleanup();
            return 2;
        }

        // Read server IP address from the file
        if (!(fileStream >> serverIP)) {
            std::cerr << "Failed to read server IP from file." << std::endl;
            fileStream.close();
            WSACleanup();
            return 3;
        }

        // Read server port from the file
        if (!(fileStream >> port)) {
            std::cerr << "Failed to read server port from file." << std::endl;
            fileStream.close();
            WSACleanup();
            return 4;
        }

        fileStream.close();
    }
    else {
        // No command-line argument, use user input
        std::cout << "Server IP Address: ";
        while (!(std::cin >> serverIP)) {
            if (std::cin.fail()) {
                WSACleanup();
                exit(EXIT_FAILURE);
            }
            std::cin.clear();
            std::cin.ignore(65535, '\n');
        }

        std::cout << "\nServer Port Number: ";
        std::cin >> std::ws;
        while (!(std::cin >> port)) {
            if (std::cin.fail() || !isdigit(std::cin.peek())) {
                WSACleanup();
                exit(EXIT_FAILURE);
            }
            std::cin.clear();
            std::cin.ignore(65535, '\n');
        }
    }

    const std::string host{ serverIP };
    const std::string portString = std::to_string(port);

    WSADATA wsaData{};
    SecureZeroMemory(&wsaData, sizeof(wsaData));

    int errorCode = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (errorCode != NO_ERROR)
    {

        return errorCode;
    }

    // Object hints indicates which protocols to use to fill in the info.
    addrinfo hints{};
    SecureZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;          // IPv4
    hints.ai_socktype = SOCK_STREAM;    // Reliable delivery
    hints.ai_protocol = IPPROTO_TCP;    // TCP

    addrinfo* info = nullptr;
    errorCode = getaddrinfo(host.c_str(), portString.c_str(), &hints, &info);
    if ((errorCode) || (info == nullptr))
    {
        WSACleanup();
        return errorCode;
    }

    SOCKET clientSocket = socket(
        info->ai_family,
        info->ai_socktype,
        info->ai_protocol);
    if (clientSocket == INVALID_SOCKET)
    {

        freeaddrinfo(info);
        WSACleanup();
        return 2;
    }

    errorCode = connect(
        clientSocket,
        info->ai_addr,
        static_cast<int>(info->ai_addrlen));
    if (errorCode == SOCKET_ERROR)
    {

        freeaddrinfo(info);
        closesocket(clientSocket);
        WSACleanup();
        return 3;
    }


    std::string text;

    std::cin.ignore(65535, '\n');
    std::cin >> std::ws;
    constexpr size_t BUFFER_SIZE = 1000;
    char buffer[BUFFER_SIZE];
    bool running = true;
    // Inside main function
    /**
     * @brief Sending Thread.
     *
     * This thread handles reading user input from the console and sending corresponding commands to the server.
     * It supports sending echo requests with '/e IP:Port Message', requesting a list of connected users with '/l',
     * and disconnecting from the server with '/q'. The thread terminates when the user decides to quit the application.
     */
    std::thread send_thread([&]() {
        while (running) {
            // Get user input
            std::getline(std::cin, text);

            if (text == "/q")
            {
                // Optionally send a quit command to the server
                unsigned char quitCommand = REQ_QUIT; // Make sure REQ_QUIT is defined correctly
                send(clientSocket, reinterpret_cast<const char*>(&quitCommand), sizeof(quitCommand), 0);

                // Now close the socket and cleanup
                shutdown(clientSocket, REQ_QUIT); // Optional but gracefully shuts down sending
                closesocket(clientSocket);
                WSACleanup();
                //std::cerr << "disconnecting..." << std::endl;
                running = false;
                break;
            }
            else if (text == "/l") {
                unsigned char listUsersCommand = CMDID::REQ_LISTUSERS; // Assuming REQ_LISTUSERS is defined correctly
                int bytesSent = send(clientSocket, reinterpret_cast<const char*>(&listUsersCommand), sizeof(listUsersCommand), 0);
                if (bytesSent == SOCKET_ERROR) {

                    break;
                }
            }
            else if (text.substr(0, 2) == "/e") {
                // Handle echo command...
                std::string ip;
                uint16_t port;
                std::string message;
                if (text.size() > 3 && text[2] == ' ') {
                    if (parseEchoCommand(text, ip, port, message)) {
                        std::vector<unsigned char> packet;
                        // Construct the packet with REQ_ECHO, IP, port, and message
                        // REQ_ECHO should be defined appropriately in your CMDID enum
                        packet.push_back(REQ_ECHO);

                        // Include the target IP addressl
                        in_addr ip_addr;
                        inet_pton(AF_INET, ip.c_str(), &ip_addr);
                        uint32_t ip_network = ip_addr.s_addr;
                        packet.insert(packet.end(), reinterpret_cast<unsigned char*>(&ip_network), reinterpret_cast<unsigned char*>(&ip_network) + sizeof(ip_network));

                        // Include the target port
                        uint16_t port_network = htons(port);
                        packet.insert(packet.end(), reinterpret_cast<unsigned char*>(&port_network), reinterpret_cast<unsigned char*>(&port_network) + sizeof(port_network));

                        // Include the message length and message
                        uint32_t message_length = htonl(static_cast<uint32_t>(message.size()));
                        packet.insert(packet.end(), reinterpret_cast<unsigned char*>(&message_length), reinterpret_cast<unsigned char*>(&message_length) + sizeof(message_length));
                        packet.insert(packet.end(), message.begin(), message.end());
                        // Debug output
                       // std::cout << "Sending to " << ip << ":" << port << " with message: " << message << std::endl;

                        // Send the packet
                        send(clientSocket, reinterpret_cast<const char*>(packet.data()), packet.size(), 0);


                    }
                    else {
                        std::cout << "==========RECV START==========" << std::endl;
                        std::cout << "Echo error" << std::endl;
                        std::cout << "==========RECV END==========" << std::endl;
                    }
                }

                else {
                    shutdown(clientSocket, SD_SEND); // Optional but gracefully shuts down sending
                    closesocket(clientSocket);
                    WSACleanup();
                    //std::cerr << "disconnecting..." << std::endl;
                    running = false;
                    break;
                }

            }
            else
            {


                // Now close the socket and cleanup
                shutdown(clientSocket, SD_SEND); // Optional but gracefully shuts down sending
                closesocket(clientSocket);
                WSACleanup();
                //std::cerr << "disconnecting..." << std::endl;
                running = false;
                break;
            }
            std::cin.clear();


        }
        });

    // Receiving thread or loop
    // Inside main function
    /**
     * @brief Receiving Thread.
     *
     * This thread continuously listens for messages from the server and processes them as they arrive.
     * It handles displaying the list of connected users and echoing messages back to the console. The thread
     * terminates upon disconnection from the server or if an error occurs during communication.
     */
    std::thread receive_thread([&]() {
        while (running) {
            int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
            if (bytesReceived == SOCKET_ERROR)
            {
                std::cerr << "disconnection...\n";
                break;
            }
            //std::cout << "Received buffer in hexadecimal: ";
            //for (int i = 0; i < bytesReceived; ++i) {
            //    std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(static_cast<unsigned char>(buffer[i])) << " ";
            //}
            //std::cout << std::dec << "\n";  // Switch back to decimal output.

            CMDID commandId = static_cast<CMDID>(buffer[0]);
            //std::cout << "Command ID: " << static_cast<unsigned>(commandId) << "\n";

            if (commandId == RSP_LISTUSERS)
            {
                std::cout << "==========RECV START==========\nUsers:" << std::endl;
                uint16_t numUsers;
                memcpy(&numUsers, buffer + 1, sizeof(numUsers));
                numUsers = ntohs(numUsers); // Convert from network byte order to host byte order


                size_t offset = 1 + sizeof(numUsers);
                for (int i = 0; i < numUsers; ++i) {
                    uint32_t ipAddr;
                    memcpy(&ipAddr, buffer + offset, sizeof(ipAddr));
                    offset += sizeof(ipAddr);

                    uint16_t port;
                    memcpy(&port, buffer + offset, sizeof(port));
                    port = ntohs(port); // Convert port to host byte order
                    offset += sizeof(port);

                    // Convert IP address to string
                    struct in_addr ip_addr_struct;
                    ip_addr_struct.s_addr = ipAddr;
                    char str_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &ip_addr_struct, str_ip, INET_ADDRSTRLEN);

                    std::cout << str_ip << ":" << port << std::endl;
                }
                std::cout << "==========RECV END==========" << std::endl;
            }
            else if (commandId == RSP_ECHO)
            {
                // Extract the source IP address
                uint32_t ipAddr;
                std::memcpy(&ipAddr, buffer + 1, sizeof(ipAddr));
                struct in_addr ip_addr_struct;
                ip_addr_struct.s_addr = ipAddr;
                char str_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &ip_addr_struct, str_ip, INET_ADDRSTRLEN);

                // Extract the source port
                uint16_t port;
                std::memcpy(&port, buffer + 5, sizeof(port));
                port = ntohs(port); // Convert port to host byte order

                // Extract the message length
                uint32_t message_length;
                std::memcpy(&message_length, buffer + 7, sizeof(message_length));
                message_length = ntohl(message_length); // Convert to host byte order

                // Extract the message
                std::string message(buffer + 11, message_length);

                std::cout << "==========RECV START==========" << std::endl;
                std::cout << str_ip << ":" << port << std::endl;
                std::cout << message << std::endl;
                std::cout << "==========RECV END==========" << std::endl;
            }
            else if (commandId == REQ_ECHO) {
                // Extract the source IP address
                std::vector<unsigned char> packet;
                packet.push_back(RSP_ECHO);
                uint32_t ipAddr;
                std::memcpy(&ipAddr, buffer + 1, sizeof(ipAddr));
                struct in_addr ip_addr_struct;
                ip_addr_struct.s_addr = ipAddr;
                char str_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &ip_addr_struct, str_ip, INET_ADDRSTRLEN);

                // Extract the source port
                uint16_t port;
                std::memcpy(&port, buffer + 5, sizeof(port));
                port = ntohs(port); // Convert port to host byte order

                // Extract the message length
                uint32_t message_length;
                std::memcpy(&message_length, buffer + 7, sizeof(message_length));
                message_length = ntohl(message_length); // Convert to host byte order

                // Extract the message
                std::string message(buffer + 11, message_length);

                std::cout << "==========RECV START==========" << std::endl;
                std::cout << str_ip << ":" << port << std::endl;
                std::cout << message << std::endl;
                std::cout << "==========RECV END==========" << std::endl;
                packet.insert(packet.end(), reinterpret_cast<unsigned char*>(&ip_addr_struct), reinterpret_cast<unsigned char*>(&ip_addr_struct) + sizeof(ip_addr_struct));
                uint16_t port_network = htons(port);
                packet.insert(packet.end(), reinterpret_cast<unsigned char*>(&port_network), reinterpret_cast<unsigned char*>(&port_network) + sizeof(port_network));
                uint32_t messagelength = htonl(static_cast<uint32_t>(message.size()));
                packet.insert(packet.end(), reinterpret_cast<unsigned char*>(&messagelength), reinterpret_cast<unsigned char*>(&messagelength) + sizeof(messagelength));
                packet.insert(packet.end(), message.begin(), message.end());
                // Send the packet
                send(clientSocket, reinterpret_cast<const char*>(packet.data()), packet.size(), 0);

            }
            else if (commandId == ECHO_ERROR) {
                std::cout << "==========RECV START==========" << std::endl;
                std::cout << "Echo error" << std::endl;
                std::cout << "==========RECV END==========" << std::endl;
            }


        }
        });
    // Wait for both threads to finish if they are used, otherwise, the loops will just run consecutively
    send_thread.join();
    receive_thread.join();
    closesocket(clientSocket);

    WSACleanup();
    return 0;
}