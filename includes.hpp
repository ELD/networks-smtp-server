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
// * #DEFINE blocks are bad, don't do it. Use static consts instead, in this case
// ************************************************************************
const static int HELO = 1;
const static int MAIL = 2;
const static int RCPT = 3;
const static int DATA = 4;
const static int RSET = 5;
const static int NOOP = 6;
const static int QUIT = 7;

// ************************************************************************
// * Local functions we are going to use.
// ************************************************************************
string readCommand(int sockfd);
int parseCommand(string commandString);
void* processConnection(void *arg);
