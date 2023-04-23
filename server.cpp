// C Headers
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
// C++ Headers
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <string>
#include <vector>
using namespace std;

//readFile - reads lines in file into the data structure lines
vector<string> readFile(){
	string line;
	vector<string> lines;
	ifstream file("test");
	while (getline(file, line)){
		lines.push_back(line);
	}
	return lines;
}

//sendFile - send file line-by-line to a client at fd
void sendFile(int fd, vector<string> lines){
	vector<string>::iterator i;

	send(fd, (void*)"Start Transfer", 1024, 0);

	//this is the same as the loop that is commented out
	for(string msg : lines){
		char buffer[1024];

		send(fd, msg.c_str(), msg.length(), 0);
		read(fd, buffer, 1024);
	}

	send(fd, (void*)"End Transfer", 1024, 0);
}

//threadFunc - thread function to read any messages from a client
void *threadFunc(void *args){
	ssize_t n;
	int clientFd = *(int*)args;
	char buffer[1024];
	pthread_detach(pthread_self());

	// Read until disconnection
	while ((n = read(clientFd, buffer, 1024)) > 0){
		string line(buffer);
		
		if (line == "exit"){
			close(clientFd);
		}
		else if (line == "get"){
			cout << "Get Received" << endl;
			sendFile(clientFd, readFile());	
		}
	}
	return NULL;
}

//handleSigInt - action performed when user types ctrl-C
void handleSigInt(int unused __attribute__((unused))){
	exit(0);
}

//main server program
int main(void){

	//setup SIGINT signal handler
	signal(SIGINT, handleSigInt);

	// Create server socket
	int serverFd;
	if ((serverFd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		std::cerr << "Error: Can't create socket." << std::endl;
		return 1;
	}

	// Set options for socket
	int val;
	if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int))){
		std::cerr << "Error: Can't reuse socket." << std::endl;
		return 2;
	}

	// Configure addr and bind
	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(10001);
	if (bind(serverFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1){
		cerr << "Error: Can't bind socket to port." << endl;
		return 3;
	}

	// Listen for client connections
	if (listen(serverFd, 20) < 0){
		cerr << "Error: Can't listen for clients." << endl;
		return 4;
	}

	// Get name and port assigned to server
	char *name = new char[1024];
	struct sockaddr_in infoAddr;
	socklen_t len = sizeof(infoAddr);
	gethostname(name, 1024);
	getsockname(serverFd, (struct sockaddr *)&infoAddr, &len);

	// Report name and port
	cout << name << ":" << ntohs(serverAddr.sin_port)<< endl;
	delete name;

	// Connect a client
	struct sockaddr_in cliAddr;
	len = sizeof(cliAddr);
	while (true){
		int clientFd = accept(serverFd, (struct sockaddr *)&serverAddr, &len);

		// Create thread to deal with client
		pthread_t thread;
		pthread_create(&thread, NULL, threadFunc, (void *)&clientFd);
	}
	return 0;
}