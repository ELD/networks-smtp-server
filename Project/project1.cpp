#include "includes.hpp"

const static int    MAXLINE = 1024;
const static int    PORT    = 10001;
const static bool   DEBUG   = true;

// ***************************************************************************
// * Read the command from the socket.
// *  Simply read a line from the socket and return it as a string.
// ***************************************************************************
string readCommand(int sockfd)
{
    char buffer[MAXLINE];
    int size = read(sockfd, buffer, MAXLINE);

    // read() doesn't put the null terminator on the string automatically... :(
    if (size >= 0) {
        buffer[size] = '\0';
    }

    return string(buffer);
}

// ***************************************************************************
// * Parse the command.
// *  Read the string and find the command, returning the number we assoicated
// *  with that command.
// ***************************************************************************
int parseCommand(string commandString)
{
    string command = "";
    int spacePos = commandString.find_first_of(' ');

    if (spacePos != string::npos) {
        command = commandString.substr(0, spacePos);
    } else {
        command = commandString;
    }

    if (command == "HELO" || command == "EHLO") {
        return HELO;
    } else if (command == "MAIL") {
        return MAIL;
    } else if (command == "RCPT") {
        return RCPT;
    } else if (command == "DATA") {
        return DATA;
    } else if (command == "RSET") {
        return RSET;
    } else if (command == "NOOP") {
        return NOOP;
    } else if (command == "QUIT") {
        return QUIT;
    }

    return -1;
}

// ***************************************************************************
// * processConnection()
// *  Master function for processing thread.
// *  !!! NOTE - the IOSTREAM library and the cout varibables may or may
// *      not be thread safe depending on your system.  I use the cout
// *      statments for debugging when I know there will be just one thread
// *      but once you are processing multiple requests it might cause problems.
// ***************************************************************************
void* processConnection(void *arg) {
    // *******************************************************
    // * This is a little bit of a cheat, but if you end up
    // * with a FD of more than 64 bits you are in trouble
    // *******************************************************
    int sockfd = *(int *) arg;
    delete (int *) arg;
    if (DEBUG)
        cout << "We are in the thread with fd = " << sockfd << endl;

    bool connectionActive   = true;
    bool seenMAIL            = false;
    bool seenRCPT            = false;
    bool seenDATA            = false;
    string forwardPath      = "";
    string reversePath      = "";
    char *messageBuffer     = nullptr;
    string cmdString = "";

    // C++11/14 lambda to reset the state of the server
    // reduces code duplication
    // It captures the outer scope by reference to allow for mutation
    auto resetState = [&](){
        seenMAIL = false;
        seenRCPT = false;
        seenDATA = false;
        forwardPath = "";
        reversePath = "";
        messageBuffer = nullptr;
    };

    string fqHostname = getFqHostname();

    // Write 220-ready code
    string message = "220 " + fqHostname + " service ready\n";
    write(sockfd, message.c_str(), message.length());

    while (connectionActive) {
        // *******************************************************
        // * Read the command from the socket.
        // *******************************************************
        cout << "Reading from socketfd = " << sockfd << endl;
        cmdString = readCommand(sockfd);
        cmdString = trim(cmdString);

        // *******************************************************
        // * Parse the command.
        // *******************************************************
        int command = parseCommand(cmdString);

        // *******************************************************
        // * Act on each of the commands we need to implement.
        // *******************************************************
        int result = -1;
        switch (command) {
            case HELO:
                doHelloCommand(sockfd, cmdString);
                break;
            case MAIL:
                resetState();
                result = doMailCommand(sockfd, cmdString, reversePath);

                if (result != 0) {
                    doError(sockfd, "501", "reverse path not well-formed");
                } else {
                    seenMAIL = true;
                    doSuccess(sockfd, "250", "reverse path ok");
                    cout << "Setting reverse path: " << reversePath << endl;
                }

                result = -1;

                break;
            case RCPT:
                // Only work if you've seen MAIL command
                if (!seenMAIL) {
                    doError(sockfd, "503", "sender info not yet given");
                } else {
                    result = doRcptCommand(sockfd, cmdString, forwardPath);
                    if (result != 0) {
                        doError(sockfd, "501", "forward path not well-formed");
                    } else {
                        seenRCPT = true;
                        doSuccess(sockfd, "250", "forward path ok");
                        cout << "Setting forward path: " << forwardPath << endl;
                    }
                }
                break;
            case DATA:
                // Only work if you've seen MAIL and RCPT command
                if (!seenRCPT) {
                    doError(sockfd, "503", "valid RCPT must precede DATA");
                } else {
                    //doDataCommand(...);
                }

                break;
            case RSET:
                resetState();
                doRsetCommand(sockfd);
                break;
            case NOOP:
                doNoopCommand(sockfd);
                break;
            case QUIT:
                doQuitCommand(sockfd, fqHostname);
                connectionActive = false;
                break;
            default:
                doUnknownCommand(sockfd);
                break;
        }
    }

    close(sockfd);

    if (DEBUG)
        cout << "Thread terminating" << endl;

}



// ***************************************************************************
// * Main
// ***************************************************************************
int main(int argc, char **argv) {

    if (argc != 1) {
        cout << "usage " << argv[0] << endl;
        exit(-1);
    }

    // *******************************************************************
    // * Creating the inital socket is the same as in a client.
    // ********************************************************************
    int listenfd = -1;
    if ((listenfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        cout << "Failed to create listening socket "
             << strerror(errno) << endl;

        exit(-1);
    }


    // ********************************************************************
    // * The same address structure is used, however we use a wildcard
    // * for the IP address since we don't know who will be connecting.
    // ********************************************************************
    struct sockaddr_in servaddr;

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family         = PF_INET;
    servaddr.sin_addr.s_addr    = htonl(INADDR_ANY);
    servaddr.sin_port           = htons(PORT);


    // ********************************************************************
    // * Binding configures the socket with the parameters we have
    // * specified in the servaddr structure.  This step is implicit in
    // * the connect() call, but must be explicitly listed for servers.
    // ********************************************************************
    if (DEBUG)
        cout << "Process has bound fd " << listenfd << " to port " << PORT << endl;

    if (bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
        cout << "bind() failed: " << strerror(errno) << endl;
        exit(-1);
    }

    // ********************************************************************
    // * Setting the socket to the listening state is the second step
    // * needed to being accepting connections.  This creates a que for
    // * connections and starts the kernel listening for connections.
    // ********************************************************************
    if (DEBUG)
        cout << "We are now listening for new connections" << endl;

    int listenq = -1;

    if (listen(listenfd, listenq) < 0) {
        cout << "listen() failed: " << strerror(errno) << endl;
        exit(-1);
    }


    // ********************************************************************
    // * The accept call will sleep, waiting for a connection.  When
    // * a connection request comes in the accept() call creates a NEW
    // * socket with a new fd that will be used for the communication.
    // ********************************************************************
    set<pthread_t*> threads;
    while (1) {
        if (DEBUG)
            cout << "Calling accept() in master thread." << endl;
        int *connfd = new int(-1);
        if ((*connfd = accept(listenfd, (struct sockaddr *) nullptr, nullptr)) < 0) {
            cout << "Accept failed: " << strerror(errno) << endl;
            exit(-1);
        }

        if (DEBUG)
            cout << "Spawing new thread to handled connect on fd=" << *connfd << endl;

        pthread_t* threadID = new pthread_t;
        pthread_create(threadID, nullptr, processConnection, (void *)connfd);
        threads.insert(threadID);
    }
}

// Code shamelessly sourced from this StackOverflow post:
// http://stackoverflow.com/questions/504810/how-do-i-find-the-current-machines-full-hostname-in-c-hostname-and-domain-info
string getFqHostname()
{
    struct addrinfo hints, *info, *p;
    int result;

    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    if ((result = getaddrinfo(hostname, "http", &hints, &info)) != 0) {
        cout << "Error: " << gai_strerror(result) << endl;
    }

    // assume only one record
    string fqHostname = string(info->ai_canonname);

    freeaddrinfo(info);

    return fqHostname;
}

void doHelloCommand(int sockfd, string const& cmdString)
{
    int hostnameStartPos = cmdString.find_first_of(' ');
    if (hostnameStartPos != string::npos) {
        string hostname = cmdString.substr(hostnameStartPos + 1);
        string message = "250 hello " + hostname + "\n";
        write(sockfd, message.c_str(), message.length());
    } else {
        string message = "501 missing argument(s)\n";
        write(sockfd, message.c_str(), message.length());
    }
}

int doMailCommand(int sockfd, string const& cmdString, string &reversePath)
{
    // Make sure FROM parameter exists
    int fromPos = cmdString.find("FROM:");
    if (fromPos == string::npos) {
        return -1;
    }

    // Find the email address in the <...> syntax
    int startBracketPos = cmdString.find('<');
    int endBracketPos = cmdString.find('>');
    int substrLength = endBracketPos - startBracketPos - 1;

    reversePath = cmdString.substr(startBracketPos + 1, substrLength);

    // Make sure the email address appears "valid"
    int atSignPos = reversePath.find('@');
    if (atSignPos == string::npos) {
        return -1;
    }

    // give a success exit code
    return 0;
}

int doRcptCommand(int sockfd, string const& cmdString, string& forwardPath)
{
    int toPos = cmdString.find("TO:");
    if (toPos == string::npos) {
        return -1;
    }

    int startBracketPos = cmdString.find('<');
    int endBracketPos = cmdString.find('>');
    int substrLength = endBracketPos - startBracketPos - 1;

    forwardPath = cmdString.substr(startBracketPos + 1, substrLength);

    int atSignPos = forwardPath.find('@');
    if (atSignPos == string::npos) {
        return -1;
    }

    return 0;
}

void doRsetCommand(int sockfd)
{
    string message = "250 reset ok\n";
    write(sockfd, message.c_str(), message.length());
}

void doNoopCommand(int sockfd)
{
    string message = "250 OK\n";
    write(sockfd, message.c_str(), message.length());
}

void doQuitCommand(int sockfd, string const& fqHostname)
{
    string message = "221 " + fqHostname + " closing connection\n";
    write(sockfd, message.c_str(), message.length());
}

void doUnknownCommand(int sockfd)
{
    string message = "500 unrecognized command\n";
    write(sockfd, message.c_str(), message.length());
}

void doError(int sockfd, string const& errorCode, string const& errorMsg)
{
    string message = errorCode + " " + errorMsg + "\n";
    write(sockfd, message.c_str(), message.length());
}

void doSuccess(int sockfd, string const& errorCode, string const& errorMsg)
{
    string message = errorCode + " " + errorMsg + '\n';
    write(sockfd, message.c_str(), message.length());
}

string trim(string &s)
{
    s.erase(s.begin(), find_if(s.begin(), s.end(),
                not1(ptr_fun<int, int>(isspace))));
    s.erase(find_if(s.rbegin(), s.rend(),
                not1(ptr_fun<int, int>(isspace))).base(), s.end());
    return s;
}

