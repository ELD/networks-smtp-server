#ifndef __INCLUDES_HPP_
#define __INCLUDES_HPP_

#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <resolv.h>
#include <netdb.h>

#include <errno.h>

#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <algorithm>

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
string getFqHostname();
void doHelloCommand(int, string const&);
int doMailCommand(int, string const&, string &);
int doRcptCommand(int, string const&, string &);
int doDataCommand(int);
void doRsetCommand(int);
void doNoopCommand(int);
void doQuitCommand(int, string const&);
void doUnknownCommand(int);
void doError(int, string const&, string const&);
void doSuccess(int, string const&, string const&);
void fetchMessageBuffer(int, string &);
int processMessage(string const &, string const &, string const &);
string trim_ref(string &);
string trim_val(string);

#endif

