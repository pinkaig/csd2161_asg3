/*!
\file server.cpp
\author Goh Pin Kai, pinkai.g, 2301388
\co-author Pek Jun Kai Gerald, p.junkaigerald, 2301334
\par pinkai.g\@digipen.edu
\par p.junkaigerald\@digipen.edu
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

- Start both Client and Server Projects
- In server set port to 9000
- In Client set IP to ip shown on server and port to 9000
- /q to quit
- /l to get list of files
- /d {ip address}:{udp port on server} hell.txt to test download