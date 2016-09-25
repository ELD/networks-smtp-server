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
    auto resetState = [&](){
        seenMAIL = false;
        seenRCPT = false;
        seenDATA = false;
        forwardPath = "";
        reversePath = "";
        messageBuffer = nullptr;
    };

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
        string domain = "gmail.com";
        switch (command) {
            case HELO:
                // TODO: Grab hostname
                write(sockfd, "Hello!\n", 8);
                break;
            case MAIL:
                // TODO: Fetch 'from' field of email and store in the reversePath
                resetState();
                reversePath = doMailCommand(sockfd, cmdString);
                cout << "Setting reverse path: " << reversePath << endl;
                // XXX Totally experimental use of res_query
                u_char result[1024];
                res_query(domain.c_str(), C_IN, T_MX, result, 1024);
                cout << "res_query result: " << result << endl;
                break;
            case RCPT:
                // Only work if you've seen MAIL command
                // TODO: Fetch 'to' field of email
                break;
            case DATA:
                // Only work if you've seen MAIL and RCPT command
                // TODO: Fetch message body
                break;
            case RSET:
                resetState();
                break;
            case NOOP:
                doNoopCommand(sockfd);
                break;
            case QUIT:
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

    bzero(&servaddr, sizeof(servaddr));
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

string doMailCommand(int sockfd, string const& cmdString)
{
    return "test@example.com";
}

void doNoopCommand(int sockfd)
{
    string message = "250 OK\n";
    write(sockfd, message.c_str(), message.length());
}

void doUnknownCommand(int sockfd)
{
    string message = "500 unrecognized command\n";
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

