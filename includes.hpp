#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>

#include <errno.h>

#include <iostream>
#include <string>
#include <set>

using namespace std;

// ************************************************************************
// * Assigning a tag to each token just makes the code easier to read.
// ************************************************************************
#define HELO 1
#define MAIL 2
#define RCPT 3
#define DATA 4
#define RSET 5
#define NOOP 6
#define QUIT 7

// ************************************************************************
// * Local functions we are going to use.
// ************************************************************************
string readCommand(int sockfd);
int parseCommand(string commandString);
void* processConnection(void *arg);
