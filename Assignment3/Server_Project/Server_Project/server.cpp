/* Start Header
*****************************************************************/
/*!
\file server.cpp
\author Goh Pin Kai, pinkai.g, 2301388
\par pinkai.g\@digipen.edu
\date  2024-02-20 
\brief This program implements a multi-threaded TCP server using Winsock.
       It listens for incoming client connections, manages them through a task queue, and handles various commands,
       such as echoing messages, listing connected users, and handling client disconnections. The server dynamically
       adjusts settings based on command-line arguments or user input and ensures safe concurrent processing using
       thread synchronization mechanisms like mutexes and condition variables. The program properly manages sockets
       and cleans up resources upon termination.

Copyright (C) 2025 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the
prior written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "Windows.h"		// Entire Win32 API...
// #include "winsock2.h"	// ...or Winsock alone
#include "ws2tcpip.h"		// getaddrinfo()

// Tell the Visual Studio linker to include the following library in linking.
// Alternatively, we could add this file to the linker command-line parameters,
// but including it in the source code simplifies the configuration.
#pragma comment(lib, "ws2_32.lib")
#include <sstream>  
#include <iostream>			// cout, cerr
#include <string>			// string
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <optional>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")

char clientIP[NI_MAXHOST];
char clientPort[NI_MAXSERV];
std::vector<std::pair<SOCKET, std::pair<std::string, std::string>>> connectedClients;

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
#define NET_BUF_SIZE 100
#define sendrecvflag 0
#define nofile "File Not Found!"
static std::mutex _stdoutMutex;
/**
 * @class TaskQueue
 * @brief Manages tasks in a multi-threaded environment.
 *
 * This class template is designed to manage a queue of tasks in a multi-threaded application.
 * It supports adding tasks to the queue and automatically processes them using a pool of worker threads.
 * The class provides thread-safe methods for task production and consumption, ensuring that tasks are
 * executed in an efficient and safe manner. It also handles thread synchronization and termination gracefully.
 *
 * @tparam TItem Type of the items (tasks) in the queue.
 * @tparam TAction Type of the action to be performed on each task.
 * @tparam TOnDisconnect Type of the action to be performed upon disconnection.
 */
void clearBuf(char* b) {
	memset(b, '\0', NET_BUF_SIZE);
}

int sendFile(FILE* fp, char* buf, int bufSize) {
	size_t bytesRead = fread(buf, 1, bufSize, fp);

	if (bytesRead > 0) {
		return static_cast<int>(bytesRead); // Return actual bytes read
	}

	return 0; // End of file
}
template <typename TItem, typename TAction, typename TOnDisconnect>
class TaskQueue {
public:

	TaskQueue(size_t workerCount, size_t slotCount, TAction& action, TOnDisconnect& onDisconnect) :
		_slotCount{ slotCount },
		_itemCount{ 0 },
		_onDisconnect{ onDisconnect },
		_stay{ true }
	{
		for (size_t i = 0; i < workerCount; ++i)
		{
			_workers.emplace_back([this, &action]() {
				this->work(*this, action);
				});
		}
	}

	~TaskQueue()
	{
		disconnect();
		for (std::thread& worker : _workers)
		{
			worker.join();
		}
	}

	void disconnect()
	{
		_stay = false;
		_onDisconnect();
	}

	void produce(TItem item)
	{
		// Non-RAII unique_lock to be blocked by a producer who needs a slot.
		{
			// Wait for an available slot...
			std::unique_lock<std::mutex> slotCountLock{ _slotCountMutex };
			_producers.wait(slotCountLock, [&]() { return _slotCount > 0; });
			--_slotCount;
		}
		// RAII lock_guard locked for buffer.
		{
			// Lock the buffer.
			std::lock_guard<std::mutex> bufferLock{ _bufferMutex };
			_buffer.push(item);
		}
		// RAII lock_guard locked for itemCount.
		{
			// Announce available item.
			std::lock_guard<std::mutex> itemCountLock(_itemCountMutex);
			++_itemCount;
			_consumers.notify_one();
		}
	}
	std::optional <TItem> consume()
	{
		std::optional<TItem> result = std::nullopt;
		// Non-RAII unique_lock to be blocked by a consumer who needs an item.
		{
			// Wait for an available item or termination...
			std::unique_lock<std::mutex> itemCountLock{ _itemCountMutex };
			_consumers.wait(itemCountLock, [&]() { return (_itemCount > 0) || (!_stay); });
			if (_itemCount == 0)
			{
				_consumers.notify_one();
				return result;
			}
			--_itemCount;
		}
		// RAII lock_guard locked for buffer.
		{
			// Lock the buffer.
			std::lock_guard<std::mutex> bufferLock{ _bufferMutex };
			result = _buffer.front();
			_buffer.pop();
		}
		// RAII lock_guard locked for slots.
		{
			// Announce available slot.
			std::lock_guard<std::mutex> slotCountLock{ _slotCountMutex };
			++_slotCount;
			_producers.notify_one();
		}
		return result;
	}
	void work(TaskQueue<TItem, TAction, TOnDisconnect>& tq, TAction& action)
	{
		while (true)
		{
			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cout
					<< "Thread ["
					<< std::this_thread::get_id()
					<< "] is waiting for a task."
					<< std::endl;
			}
			std::optional<TItem> item = tq.consume();
			if (!item)
			{
				// Termination of idle threads.
				break;
			}

			{
				std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
				std::cout
					<< "Thread ["
					<< std::this_thread::get_id()
					<< "] is executing a task."
					<< std::endl;
			}

			if (!action(*item))
			{
				// Decision to terminate workers.
				tq.disconnect();
			}
		}

		{
			std::lock_guard<std::mutex> usersLock{ _stdoutMutex };
			std::cout
				<< "Thread ["
				<< std::this_thread::get_id()
				<< "] is exiting."
				<< std::endl;
		}
	}
private:

	// Pool of worker threads.
	std::vector<std::thread> _workers;

	// Buffer of slots for items.
	std::mutex _bufferMutex;
	std::queue<TItem> _buffer;

	// Count of available slots.
	std::mutex _slotCountMutex;
	size_t _slotCount;
	// Critical section condition for decreasing slots.
	std::condition_variable _producers;

	// Count of available items.
	std::mutex _itemCountMutex;
	size_t _itemCount;
	// Critical section condition for decreasing items.
	std::condition_variable _consumers;

	volatile bool _stay;

	TOnDisconnect& _onDisconnect;
};


bool execute(SOCKET clientSocket);
void disconnect(SOCKET& listenerSocket);
/**
 * @brief Entry point for the server application.
 *
 * Initializes Winsock, sets up the server to listen on a specified port, and accepts client connections.
 * Once a client is connected, it handles client requests through a TaskQueue, processing each request
 * in separate threads. Supports dynamic adjustment of server settings based on command line arguments
 * or user input during runtime.
 *
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings.
 * @return int Returns 0 on successful execution, non-zero on error.
 */
int main(int argc, char* argv[])
{
	uint16_t port = 2048;
	// Set the server IP address
	std::string serverIP = "192.168.15.1";
	std::string portString;
	if (argc >= 2) {
		// Command-line argument provided, use it as the input file
		const char* inputFile = argv[1];

		// Open the input file
		std::ifstream fileStream(inputFile);
		if (!fileStream.is_open()) {
			std::cerr << "Failed to open input file: " << inputFile << std::endl;
			WSACleanup();
			return 2;
		}

		// Read server port from the file
		if (!(fileStream >> port)) {
			std::cerr << "Failed to read server port from file." << std::endl;
			fileStream.close();
			WSACleanup();
			return 4;
		}
		else {

			std::cout << "Server Port Number: " << port;

		}

		fileStream.close();
	}
	else if (argc < 2) {
		if (argc == 1)
			std::cout << "Server Port Number: ";
		while (!(std::cin >> port))
		{
			if (std::cin.fail() || !isdigit(std::cin.peek()))
			{
				WSACleanup();
				exit(EXIT_FAILURE);
			}
			std::cin.clear();
			std::cin.ignore(6655667, '\n');
		}
	}
	// Convert the port to a string
	portString = std::to_string(port);

	// -------------------------------------------------------------------------
	// Start up Winsock, asking for version 2.2.
	//
	// WSAStartup()
	// -------------------------------------------------------------------------

	// This object holds the information about the version of Winsock that we
	// are using, which is not necessarily the version that we requested.
	WSADATA wsaData{};

	// Initialize Winsock. You must call WSACleanup when you are finished.
	// As this function uses a reference counter, for each call to WSAStartup,
	// you must call WSACleanup or suffer memory issues.
	int errorCode = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (errorCode != NO_ERROR)
	{

		return errorCode;
	}


	// -------------------------------------------------------------------------
	// Resolve own host name into IP addresses (in a singly-linked list).
	//
	// getaddrinfo()
	// -------------------------------------------------------------------------

	// Object hints indicates which protocols to use to fill in the info.
	addrinfo hints{};
	SecureZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;			// IPv4
	// For UDP use SOCK_DGRAM instead of SOCK_STREAM.
	hints.ai_socktype = SOCK_STREAM;	// Reliable delivery
	// Could be 0 for autodetect, but reliable delivery over IPv4 is always TCP.
	hints.ai_protocol = IPPROTO_TCP;	// TCP
	// Create a passive socket that is suitable for bind() and listen().
	hints.ai_flags = AI_PASSIVE;
	// Get the local hostname
	char hostname[NI_MAXHOST];
	if (gethostname(hostname, NI_MAXHOST) != 0)
	{

		WSACleanup();
		return 6;
	}
	addrinfo* info = nullptr;
	errorCode = getaddrinfo(hostname, portString.c_str(), &hints, &info);
	if ((errorCode) || (info == nullptr))
	{

		WSACleanup();
		return errorCode;
	}


	// -------------------------------------------------------------------------
	// Create a socket and bind it to own network interface controller.
	//
	// socket()
	// bind()
	// -------------------------------------------------------------------------

	SOCKET listenerSocket = socket(
		hints.ai_family,
		hints.ai_socktype,
		hints.ai_protocol);
	if (listenerSocket == INVALID_SOCKET)
	{

		freeaddrinfo(info);
		WSACleanup();
		return 1;
	}

	errorCode = bind(
		listenerSocket,
		info->ai_addr,
		static_cast<int>(info->ai_addrlen));
	if (errorCode != NO_ERROR)
	{

		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}

	freeaddrinfo(info);

	if (listenerSocket == INVALID_SOCKET)
	{

		WSACleanup();
		return 2;
	}

	// Get the local address of the bound socket
	sockaddr_storage localAddr;
	int localAddrLen = sizeof(localAddr);
	getsockname(listenerSocket, reinterpret_cast<sockaddr*>(&localAddr), &localAddrLen);

	char localIP[NI_MAXHOST];
	char localPort[NI_MAXSERV];
	getnameinfo(
		reinterpret_cast<sockaddr*>(&localAddr),
		localAddrLen,
		localIP,
		NI_MAXHOST,
		localPort,
		NI_MAXSERV,
		NI_NUMERICHOST | NI_NUMERICSERV);

	std::cout << "\nServer IP Address: " << localIP << std::endl;
	std::cout << "Server Port Number: " << localPort << std::endl;
	// -------------------------------------------------------------------------
	// Set a socket in a listening mode and accept 1 incoming client.
	//
	// listen()
	// accept()
	// -------------------------------------------------------------------------

	errorCode = listen(listenerSocket, SOMAXCONN);
	if (errorCode != NO_ERROR)
	{

		closesocket(listenerSocket);
		WSACleanup();
		return 3;
	}

	{
		const auto onDisconnect = [&]() { disconnect(listenerSocket); };
		auto tq = TaskQueue<SOCKET, decltype(execute), decltype(onDisconnect)>{ 10, 20, execute, onDisconnect };
		while (listenerSocket != INVALID_SOCKET)
		{
			sockaddr clientAddress{};
			SecureZeroMemory(&clientAddress, sizeof(clientAddress));
			int clientAddressSize = sizeof(clientAddress);
			SOCKET clientSocket = accept(
				listenerSocket,
				&clientAddress,
				&clientAddressSize);
			sockaddr_in clientAddr;
			int clientAddrLen = sizeof(clientAddr);
			getpeername(clientSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);

			getnameinfo(
				reinterpret_cast<sockaddr*>(&clientAddr),
				clientAddrLen,
				clientIP,
				NI_MAXHOST,
				clientPort,
				NI_MAXSERV,
				NI_NUMERICHOST | NI_NUMERICSERV);
			connectedClients.emplace_back(clientSocket, std::make_pair(clientIP, clientPort));

			std::cout << "\nClient IP Address: " << clientIP << std::endl;
			std::cout << "Client Port Number: " << clientPort << std::endl;
			if (clientSocket == INVALID_SOCKET)
			{
				break;
			}
			tq.produce(clientSocket);
		}
	}

	// -------------------------------------------------------------------------
	// Clean-up after Winsock.
	//
	// WSACleanup()
	// -------------------------------------------------------------------------

	WSACleanup();
}
/**
 * @brief Closes and cleans up the listener socket.
 *
 * Shuts down the listener socket and performs necessary cleanup. This function is called when the server
 * is ready to stop accepting new connections and is shutting down.
 *
 * @param listenerSocket Reference to the listener socket to be closed.
 */

void disconnect(SOCKET& listenerSocket)
{

	if (listenerSocket != INVALID_SOCKET)
	{
		shutdown(listenerSocket, SD_BOTH);
		closesocket(listenerSocket);
		listenerSocket = INVALID_SOCKET;
	}
}
/**
 * @brief Removes a disconnected client from the list of connected clients.
 *
 * Iterates through the list of connected clients and removes the entry corresponding to the clientSocket
 * that has been disconnected. This ensures the list of connected clients is always up to date.
 *
 * @param clientSocket The socket associated with the client that has disconnected.
 */

void removeDisconnectedClient(SOCKET clientSocket) {
	//std::lock_guard<std::mutex> lock(connectedClientsMutex); // Use a mutex if access is multithreaded

	auto it = std::remove_if(connectedClients.begin(), connectedClients.end(),
		[clientSocket](const std::pair<SOCKET, std::pair<std::string, std::string>>& client) {
			return client.first == clientSocket;
		});

	if (it != connectedClients.end()) {
		connectedClients.erase(it, connectedClients.end());
	}
}
/**
 * @brief Handles client requests.
 *
 * Processes client requests by receiving commands, performing the requested action, and sending back responses.
 * Supports commands such as REQ_QUIT (client disconnection), REQ_ECHO (echo message), REQ_LISTUSERS (list all connected users),
 * and handles them accordingly. This function is intended to be executed in a separate thread for each client.
 *
 * @param clientSocket The socket associated with the client.
 * @return bool Returns true to keep the server running, false to initiate server shutdown.
 */
bool execute(SOCKET clientSocket)
{

	// -------------------------------------------------------------------------
	// Receive some text and send it back.
	//
	// recv()
	// send()
	// -------------------------------------------------------------------------

	constexpr size_t BUFFER_SIZE = 1000;
	char buffer[BUFFER_SIZE];
	bool stay = true;
	char ipStr[INET_ADDRSTRLEN];
	while (true)
	{
		const int bytesReceived = recv(
			clientSocket,
			buffer,
			BUFFER_SIZE - 1,
			0);
		if (bytesReceived == SOCKET_ERROR)
		{
			//std::cerr << "Graceful shutdown." << std::endl;
			removeDisconnectedClient(clientSocket);
			break;
		}


		// Ensure network byte order is converted to host byte order
		// Determine the command ID
#ifdef DEBUG_ASSIGNMENT2_TEST
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(5000ms); // Delay for 5 seconds
#endif
		CMDID commandId = static_cast<CMDID>(buffer[0]);
		char net_buf[NET_BUF_SIZE];
		sockaddr_in client_addr;
		int client_addrlen = sizeof(client_addr);
		// Handle REQ_LISTUSERS command
		if (commandId == REQ_LISTFILES) {
			std::vector<unsigned char> response;
			response.push_back(RSP_LISTFILES);  // Command ID for response

			std::vector<std::string> fileList;
			WIN32_FIND_DATAA findFileData;
			HANDLE hFind = FindFirstFileA("*.*", &findFileData);

			if (hFind == INVALID_HANDLE_VALUE) {
				std::cerr << "Failed to list files." << std::endl;
				uint16_t zeroFiles = htons(0);
				response.insert(response.end(), reinterpret_cast<unsigned char*>(&zeroFiles),
					reinterpret_cast<unsigned char*>(&zeroFiles) + sizeof(zeroFiles));
			}
			else {
				do {
					if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
						std::string fileName(findFileData.cFileName);
						fileList.push_back(fileName);
					}
				} while (FindNextFileA(hFind, &findFileData) != 0);
				FindClose(hFind);

				uint16_t numFiles = htons(static_cast<uint16_t>(fileList.size()));
				response.insert(response.end(), reinterpret_cast<unsigned char*>(&numFiles),
					reinterpret_cast<unsigned char*>(&numFiles) + sizeof(numFiles));

				for (const auto& file : fileList) {
					uint16_t fileNameLength = htons(static_cast<uint16_t>(file.length()));
					response.insert(response.end(), reinterpret_cast<unsigned char*>(&fileNameLength),
						reinterpret_cast<unsigned char*>(&fileNameLength) + sizeof(fileNameLength));
					response.insert(response.end(), file.begin(), file.end());
				}
			}

			send(clientSocket, reinterpret_cast<char*>(response.data()), static_cast<int>(response.size()), 0);
		}
		else if (commandId == REQ_DOWNLOAD) {
			std::vector<unsigned char> response;
			response.push_back(RSP_DOWNLOAD);  // Correct Command ID for response
			std::cout << "\nProcessing download request..." << std::endl;

			// Receive command + filename from client
			//clearBuf(net_buf);
			int nBytes = recvfrom(clientSocket, net_buf, NET_BUF_SIZE, sendrecvflag,
				(struct sockaddr*)&client_addr, &client_addrlen);

			if (nBytes == SOCKET_ERROR) {
				std::cerr << "Receive failed: " << WSAGetLastError() << std::endl;
				return false;
			}

			// Extract Command ID (first byte)
			CMDID receivedCommand = static_cast<CMDID>(net_buf[0]);

			// Extract Filename (everything after first byte)
			std::string fileName(net_buf + 1);

			std::cout << "Command Received: " << static_cast<int>(receivedCommand) << std::endl;
			std::cout << "Requested File: " << fileName << std::endl;

			// Open the file in binary mode
			FILE* fp;
			errno_t err = fopen_s(&fp, fileName.c_str(), "rb");
			if (err != 0 || fp == NULL) {
				std::cerr << "File open failed!" << std::endl;
				strcpy_s(net_buf, NET_BUF_SIZE, nofile);
				net_buf[strlen(nofile)] = EOF; // Mark end of file
				sendto(clientSocket, net_buf, NET_BUF_SIZE, sendrecvflag,
					(struct sockaddr*)&client_addr, client_addrlen);
			}
			else {
				std::cout << "File successfully opened!" << std::endl;

				// Send file data
				while (true) {
					clearBuf(net_buf); // Clear buffer before reading
					int bytesRead = sendFile(fp, net_buf, NET_BUF_SIZE);
					if (bytesRead <= 0) {
						break; // End of file or error
					}

					sendto(clientSocket, net_buf, bytesRead, sendrecvflag,
						(struct sockaddr*)&client_addr, client_addrlen);
				}

				fclose(fp);
			}
		}

		else if (commandId == REQ_QUIT) {
			removeDisconnectedClient(clientSocket);

		}
		else {
			std::cerr << "Graceful shutdown." << std::endl;
			removeDisconnectedClient(clientSocket);

			break;
		}


	}


	// -------------------------------------------------------------------------
	// Shut down and close sockets.
	//
	// shutdown()
	// closesocket()
	// -------------------------------------------------------------------------

	shutdown(clientSocket, SD_BOTH);
	closesocket(clientSocket);
	return stay;
}
