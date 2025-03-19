/* Start Header
*****************************************************************/
/*!
\file client.cpp
/* Start Header
*****************************************************************/
/*!
\file client.cpp
\author Goh Pin Kai, pinkai.g, 2301388
\co-author Pek Jun Kai Gerald, p.junkaigerald, 2301334
\par pinkai.g\@digipen.edu
\par p.junkaigerald\@digipen.edu
\date  2024-03-16
\brief This program implements a TCP and UDP client using Winsock for communication with a server.
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

/*Copyright(C) 2025 DigiPen Institute of Technology.
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
#include <mutex>
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
    REQ_DOWNLOAD = (unsigned char)0x2,
    RSP_DOWNLOAD = (unsigned char)0x3,
    REQ_LISTFILES = (unsigned char)0x4,
    RSP_LISTFILES = (unsigned char)0x5,
    CMD_TEST = (unsigned char)0x20,
    DOWNLOAD_ERROR = (unsigned char)0x30
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

#define NET_BUF_SIZE 4096
#define sendrecvflag 0
std::mutex mtx; // Mutex for protecting shared resources
char net_buf[NET_BUF_SIZE];

void clearBuf(char* b) {
    memset(b, '\0', NET_BUF_SIZE);
}

bool saveFile(FILE* fp, char* buf, int s) {
    std::lock_guard<std::mutex> lock(mtx);
    for (int i = 0; i < s; i++) {
        char ch = buf[i];
        if (ch == EOF) {
            return true; // End of file
        }
        else {
            fputc(ch, fp); // Write the character to the file
        }
    }
    return false;
}

void receiveData(SOCKET udpSocket, sockaddr_in addr_con, const std::string& fileName) {
    char net_buf[NET_BUF_SIZE];
    int addrlen = sizeof(addr_con);

    // Open the file for writing in binary mode
    FILE* fp;
    errno_t err = fopen_s(&fp, fileName.c_str(), "wb");
    if (err != 0 || fp == nullptr) {
        std::cerr << "Failed to open file for writing: " << fileName << std::endl;
        return;
    }

    std::cout << "Receiving file: " << fileName << " ..." << std::endl;

    while (true) {
        clearBuf(net_buf);
        int nBytes = recvfrom(udpSocket, net_buf, NET_BUF_SIZE, sendrecvflag, (struct sockaddr*)&addr_con, &addrlen);
        if (nBytes <= 0) {
            break; // Connection closed or error
        }
        if (saveFile(fp, net_buf, NET_BUF_SIZE)) {
            break; // End of file reached
        }

        fwrite(net_buf, 1, nBytes, fp); // Write only the received bytes
    }

    fclose(fp);
    std::cout << "File downloaded successfully: " << fileName << std::endl;
}
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

    // Initialize UDP socket
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create UDP socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
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
            sockaddr_in udpAddr = {};
            udpAddr.sin_family = AF_INET;
            udpAddr.sin_addr.s_addr = INADDR_ANY;
            udpAddr.sin_port = htons(9010);
            sockaddr_in addr_con;
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
                unsigned char listFilesCommand = CMDID::REQ_LISTFILES;
                int bytesSent = send(clientSocket, reinterpret_cast<const char*>(&listFilesCommand), sizeof(listFilesCommand), 0);

                if (bytesSent == SOCKET_ERROR) {
                    std::cerr << "Failed to send list files request: " << WSAGetLastError() << std::endl;
                    break;
                }

                // Receiving response
                constexpr size_t BUFFER_SIZE = 4096; // Increase buffer size
                char buffer[BUFFER_SIZE];

                int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
                if (bytesReceived == SOCKET_ERROR) {
                    std::cerr << "Failed to receive file list: " << WSAGetLastError() << std::endl;
                    break;
                }

                if (buffer[0] == RSP_LISTFILES) {
                    uint16_t numFiles;
                    memcpy(&numFiles, buffer + 1, sizeof(numFiles));
                    numFiles = ntohs(numFiles);

                    size_t offset = 1 + sizeof(numFiles);
                    std::cout << "==========RECV START==========\nFiles:" << std::endl;

                    for (int i = 0; i < numFiles; ++i) {
                        uint16_t fileNameLength;
                        memcpy(&fileNameLength, buffer + offset, sizeof(fileNameLength));
                        fileNameLength = ntohs(fileNameLength);
                        offset += sizeof(fileNameLength);

                        std::string fileName(buffer + offset, fileNameLength);
                        offset += fileNameLength;

                        std::cout << fileName << std::endl;
                    }

                    std::cout << "==========RECV END==========" << std::endl;
                }
            }
            else if (text.substr(0, 2) == "/d") {
    std::istringstream iss(text);
    std::string cmd, serverAddrStr, fileName;
    iss >> cmd >> serverAddrStr >> fileName;

    if (cmd != "/d" || serverAddrStr.empty() || fileName.empty()) {
        std::cerr << "Invalid format. Usage: /d <IP>:<Port> <Filename>" << std::endl;
        continue;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;

    // Extract IP and port from serverAddrStr
    size_t colonPos = serverAddrStr.find(':');
    if (colonPos == std::string::npos) {
        std::cerr << "Invalid address format. Use: /d IP:PORT filename" << std::endl;
        continue;
    }
    
    std::string ip = serverAddrStr.substr(0, colonPos);
    int port = std::stoi(serverAddrStr.substr(colonPos + 1));
    serverAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address format." << std::endl;
        continue;
    }

    char net_buf[NET_BUF_SIZE];
    memset(net_buf, 0, NET_BUF_SIZE);
    
    // Set the command ID in the buffer first
    net_buf[0] = static_cast<unsigned char>(REQ_DOWNLOAD);
    strncpy_s(net_buf + 1, NET_BUF_SIZE - 1, fileName.c_str(), NET_BUF_SIZE - 2);

    // Send file request to the server via UDP
    if (sendto(clientSocket, net_buf, NET_BUF_SIZE, sendrecvflag,
        (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Send failed: " << WSAGetLastError() << std::endl;
        continue;
    }

    std::cout << "Downloading file: " << fileName << " from " << serverAddrStr << std::endl;

    // Start a thread to receive data and save it to a file
    std::thread receiverThread(receiveData, udpSocket, serverAddr, fileName);
    receiverThread.join();


}
            else
            {
                // Now close the socket and cleanup
                shutdown(clientSocket, SD_SEND); // Optional but gracefully shuts down sending
                closesocket(udpSocket);
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

            if (commandId == RSP_LISTFILES)
            {
                std::cout << "==========RECV START==========\nFiles:" << std::endl;
                uint16_t numFiles;
                memcpy(&numFiles, buffer + 1, sizeof(numFiles));
                numFiles = ntohs(numFiles); // Convert from network byte order to host byte order

                size_t offset = 1 + sizeof(numFiles);
                for (int i = 0; i < numFiles; ++i) {
                    uint16_t fileNameLength;
                    memcpy(&fileNameLength, buffer + offset, sizeof(fileNameLength));
                    fileNameLength = ntohs(fileNameLength);
                    offset += sizeof(fileNameLength);

                    std::string fileName(buffer + offset, fileNameLength);
                    offset += fileNameLength;

                    std::cout << fileName << std::endl;
                }
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