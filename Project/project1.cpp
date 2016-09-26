#include "includes.hpp"

const static int MAXLINE = 1024;
const static int PORT = 10001;
const static int SMTP_PORT = 25;
const static bool DEBUG = true;
const static string fqHostname = getFqHostname();

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
    }
    else {
        command = commandString;
    }

    if (command == "HELO" || command == "EHLO") {
        return HELO;
    }
    else if (command == "MAIL") {
        return MAIL;
    }
    else if (command == "RCPT") {
        return RCPT;
    }
    else if (command == "DATA") {
        return DATA;
    }
    else if (command == "RSET") {
        return RSET;
    }
    else if (command == "NOOP") {
        return NOOP;
    }
    else if (command == "QUIT") {
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
void *processConnection(void *arg)
{
    // *******************************************************
    // * This is a little bit of a cheat, but if you end up
    // * with a FD of more than 64 bits you are in trouble
    // *******************************************************
    int sockfd = *(int *)arg;
    delete (int *)arg;
    if (DEBUG)
        cout << "We are in the thread with fd = " << sockfd << endl;

    bool connectionActive = true;
    bool seenMAIL = false;
    bool seenRCPT = false;
    bool seenDATA = false;
    string forwardPath = "";
    string reversePath = "";
    string messageBuffer = "";
    string cmdString = "";

    // C++11/14 lambda to reset the state of the server
    // reduces code duplication
    // It captures the outer scope by reference to allow for mutation
    auto resetState = [&]() {
        seenMAIL = false;
        seenRCPT = false;
        seenDATA = false;
        forwardPath = "";
        reversePath = "";
        messageBuffer = "";
    };

    // Write 220-ready code
    string message = "220 " + fqHostname + " service ready\n";
    write(sockfd, message.c_str(), message.length());

    while (connectionActive) {
        // *******************************************************
        // * Read the command from the socket.
        // *******************************************************
        cout << "Reading from socketfd = " << sockfd << endl;
        cmdString = readCommand(sockfd);
        cmdString = trim_ref(cmdString);

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
            }
            else {
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
            }
            else {
                result = doRcptCommand(sockfd, cmdString, forwardPath);
                if (result < 0) {
                    doError(sockfd, "501", "forward path not well-formed");
                }
                else {
                    seenRCPT = true;
                    if (isLocalRecipient(forwardPath)) {
                        doSuccess(sockfd, "250", "forward path ok");
                    }
                    else {
                        doSuccess(sockfd, "251", "recipient not local, will attempt to forward");
                    }
                }
            }
            break;
        case DATA:
            // Only work if you've seen MAIL and RCPT command
            if (!seenRCPT) {
                doError(sockfd, "503", "valid RCPT must precede DATA");
            }
            else {
                doSuccess(sockfd, "354", "Start mail input; end with <CRLF>.<CRLF>");
                fetchMessageBuffer(sockfd, messageBuffer);
                processMessage(sockfd, reversePath, forwardPath, messageBuffer);

                // if (result != 0) {
                //     doError(sockfd, "451", "Local error in processing");
                // }
                // else {
                //     doSuccess(sockfd, "250", "OK");
                // }
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
int main(int argc, char **argv)
{

    if (argc != 1) {
        cout << "usage " << argv[0] << endl;
        exit(-1);
    }

    // *******************************************************************
    // * Creating the inital socket is the same as in a client.
    // ********************************************************************
    int listenfd = -1;
    if ((listenfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        cout << "Failed to create listening socket " << strerror(errno) << endl;

        exit(-1);
    }

    // ********************************************************************
    // * The same address structure is used, however we use a wildcard
    // * for the IP address since we don't know who will be connecting.
    // ********************************************************************
    struct sockaddr_in servaddr;

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = PF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    // ********************************************************************
    // * Binding configures the socket with the parameters we have
    // * specified in the servaddr structure.  This step is implicit in
    // * the connect() call, but must be explicitly listed for servers.
    // ********************************************************************
    if (DEBUG)
        cout << "Process has bound fd " << listenfd << " to port " << PORT << endl;

    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
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
    set<pthread_t *> threads;
    while (1) {
        if (DEBUG)
            cout << "Calling accept() in master thread." << endl;
        int *connfd = new int(-1);
        if ((*connfd = accept(listenfd, (struct sockaddr *)nullptr, nullptr)) < 0) {
            cout << "Accept failed: " << strerror(errno) << endl;
            exit(-1);
        }

        if (DEBUG)
            cout << "Spawing new thread to handled connect on fd=" << *connfd << endl;

        pthread_t *threadID = new pthread_t;
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

void doHelloCommand(int sockfd, string const &cmdString)
{
    int hostnameStartPos = cmdString.find_first_of(' ');
    if (hostnameStartPos != string::npos) {
        string hostname = cmdString.substr(hostnameStartPos + 1);
        string message = "250 hello " + hostname + "\n";
        write(sockfd, message.c_str(), message.length());
    }
    else {
        string message = "501 missing argument(s)\n";
        write(sockfd, message.c_str(), message.length());
    }
}

int doMailCommand(int sockfd, string const &cmdString, string &reversePath)
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

int doRcptCommand(int sockfd, string const &cmdString, string &forwardPath)
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

void doQuitCommand(int sockfd, string const &fqHostname)
{
    string message = "221 " + fqHostname + " closing connection\n";
    write(sockfd, message.c_str(), message.length());
}

void doUnknownCommand(int sockfd)
{
    string message = "500 unrecognized command\n";
    write(sockfd, message.c_str(), message.length());
}

void doError(int sockfd, string const &errorCode, string const &errorMsg)
{
    string message = errorCode + " " + errorMsg + "\n";
    write(sockfd, message.c_str(), message.length());
}

void doSuccess(int sockfd, string const &errorCode, string const &errorMsg)
{
    string message = errorCode + " " + errorMsg + '\n';
    write(sockfd, message.c_str(), message.length());
}

void fetchMessageBuffer(int sockfd, string &msgBuffer)
{
    bool keepGoing = true;

    char readBuffer[1024];
    while (keepGoing) {
        memset(&readBuffer, 0, 1024);
        int length = read(sockfd, readBuffer, 1024);
        if (length >= 0) {
            readBuffer[length] = '\0';
        }

        if (trim_val(string(readBuffer)) != ".") {
            msgBuffer += string(readBuffer);
        }
        else {
            keepGoing = false;
        }
    }
}

void processMessage(int sockfd, string const &reversePath, string const &forwardPath, string const &message)
{
    int result = -1;
    if (isLocalRecipient(forwardPath)) {
        result = writeToLocalFilesystem(reversePath, forwardPath, message);

        if (result != 0) {
            doError(sockfd, "451", "Local error in processing");
        }
        else {
            doSuccess(sockfd, "250", "OK");
        }
    }
    else {
        result = attemptToRelay(reversePath, forwardPath, message);

        if (result != 0) {
            doError(sockfd, "554", "unable to relay successfully");
        }
        else {
            doSuccess(sockfd, "250", "OK");
        }
    }
}

int writeToLocalFilesystem(const string &reversePath, const string &forwardPath, const string &message)
{
    // Get username@hostname
    int atSignPos = forwardPath.find('@');
    if (atSignPos == string::npos) {
        return -1;
    }

    string username = forwardPath.substr(0, atSignPos);
    // Create or open in append file 'username'
    fstream f(username, ios_base::out | ios_base::app);
    cout << "Creating or opening file with name " << username << endl;
    // Generate timestamp
    time_t rawtime;
    struct tm *timeInfo;
    char timestamp[80];

    time(&rawtime);
    timeInfo = localtime(&rawtime);

    strftime(timestamp, 80, "%c", timeInfo);
    string headerTimestamp = string(timestamp);

    strftime(timestamp, 80, "%a, %d %b %Y %T %z", timeInfo);
    string dateTimestamp = string(timestamp);

    // Dump message buffer into file 'username'
    f << "From " << reversePath << " " << headerTimestamp << endl;
    f << "Date: " << dateTimestamp << endl;
    f << message << endl;
    f << endl;
    f.close();

    // Return success or failure
    return 0;
}

// TODO: Error handling mid-relay
int attemptToRelay(const string &reversePath, const string &forwardPath, const string &mailMessage)
{
    int atSignPos = forwardPath.find('@');
    if (atSignPos == string::npos) {
        return -1;
    }

    string hostname = forwardPath.substr(atSignPos + 1);

    // Look up MX record
    string mxHostname;
    if (getMxRecord(hostname, mxHostname) < 0) {
        return -1;
    }

    if (DEBUG) {
        cout << "Got MX record" << mxHostname << endl;
    }

    // Create client-socket connection to MTA
    // Step 1
    int lfd = -1;
    if ((lfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    if (DEBUG) {
        cout << "Created a client-socket of fd = " << lfd << endl;
    }

    // Step 2
    struct sockaddr_in clientaddr;
    struct hostent *server;

    server = gethostbyname(mxHostname.c_str());

    memset(&clientaddr, 0, sizeof(clientaddr));

    clientaddr.sin_family = PF_INET;
    memcpy((char *)&clientaddr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    clientaddr.sin_port = htons(SMTP_PORT);

    // Step 3
    if (connect(lfd, (sockaddr *)&clientaddr, sizeof(clientaddr)) < 0) {
        cout << strerror(errno) << endl;
        return -1;
    }

    if (DEBUG) {
        cout << "Connected on fd = " << lfd << endl;
    }

    // Relay commands, check for errors - read & write to the file descriptor
    string message;
    string replyStr;
    char reply[1024];
    int len = -1;

    auto quitRemote = [&](){
        // Write QUIT
        message = "QUIT\r\n";
        write(lfd, message.c_str(), message.length());
        // Read response
        len = read(lfd, reply, 1023);
        if (len >= 0) {
            reply[len] = '\0';
        }

        // Close socket
        close(lfd);
    };

    // Read connection message first
    len = read(lfd, reply, 1023);
    if (len >= 0) {
        reply[len] = '\0';
    }
    replyStr = reply;
    if (replyStr.substr(0,3) != "220") {
        cout << "failed on connect" << endl;
        quitRemote();
        return -1;
    }

    // Write EHLO
    message = "HELO " + fqHostname + "\r\n";
    write(lfd, message.c_str(), message.length());

    // Read response
    len = read(lfd, reply, 1023);
    if (len >= 0) {
        reply[len] = '\0';
    }
    replyStr = reply;
    if (replyStr.substr(0,3) != "250") {
        cout << "failed on helo command" << endl;
        quitRemote();
        return -1;
    }

    // Write MAIL FROM:<>
    message = "MAIL FROM:<" + reversePath + ">\r\n";
    write(lfd, message.c_str(), message.length());
    // Read response
    len = read(lfd, reply, 1023);
    if (len >= 0) {
        reply[len] = '\0';
    }
    replyStr = reply;
    if (replyStr.substr(0,3) != "250") {
        cout << "failed on MAIL FROM command" << endl;
        quitRemote();
        return -1;
    }

    // Write RCPT TO:<>
    message = "RCPT TO:<" + forwardPath + ">\r\n";
    write(lfd, message.c_str(), message.length());
    // Read response
    len = read(lfd, reply, 1023);
    if (len >= 0) {
        reply[len] = '\0';
    }
    replyStr = reply;
    if (replyStr.substr(0,3) != "250" && replyStr.substr(0,3) != "251") {
        cout << "failed on RCPT TO command" << endl;
        quitRemote();
        return -1;
    }

    // Write DATA
    message = "DATA\r\n";
    write(lfd, message.c_str(), message.length());
    // Read response
    len = read(lfd, reply, 1023);
    if (len >= 0) {
        reply[len] = '\0';
    }
    replyStr = reply;
    if (replyStr.substr(0,3) != "354") {
        cout << "Failed on DATA command" << endl;
        quitRemote();
        return -1;
    }

    // Write message
    message = mailMessage + ".\r\n";
    write(lfd, message.c_str(), message.length());
    // Read response
    len = read(lfd, reply, 1023);
    if (len >= 0) {
        reply[len] = '\0';
    }
    replyStr = reply;
    if (replyStr.substr(0,3) != "250") {
        cout << "Failed while submitting message" << endl;
        quitRemote();
        return -1;
    }

    quitRemote();

    // Return success
    return 0;
}

bool isLocalRecipient(string const &forwardPath)
{
    int atSignPos = forwardPath.find('@');

    string hostname = forwardPath.substr(atSignPos + 1);

    if (hostname != "localhost") {
        return false;
    }

    return true;
}

int getMxRecord(string const &hostname, string &mxResult)
{
    // Lookup MX record for hostname
    // Comes from: http://stackoverflow.com/questions/1688432/querying-mx-record-in-c-linux
    // I figured it was fine to look this up on SO because we didn't explicitly cover this in class
    int limit = 1;
    unsigned char response[NS_PACKETSZ];
    ns_msg handle;
    ns_rr rr;
    int mx_index, ns_index, len;
    char dispbuf[4096];

    if ((len = res_search(hostname.c_str(), C_IN, T_MX, response, sizeof(response))) < 0) {
        return -1;
    }

    if (ns_initparse(response, len, &handle) < 0) {
        return -1;
    }

    len = ns_msg_count(handle, ns_s_an);
    if (len < 0) {
        return -1;
    }

    for (mx_index = 0, ns_index = 0; mx_index < limit && ns_index < len; ns_index++) {
        if (ns_parserr(&handle, ns_s_an, ns_index, &rr)) {
            continue;
        }
        ns_sprintrr(&handle, &rr, NULL, NULL, dispbuf, sizeof(dispbuf));
        if (ns_rr_class(rr) == ns_c_in && ns_rr_type(rr) == ns_t_mx) {
            char mxname[MAXDNAME];
            dn_expand(ns_msg_base(handle), ns_msg_base(handle) + ns_msg_size(handle), ns_rr_rdata(rr) + NS_INT16SZ,
                      mxname, sizeof(mxname));
            mxResult = strdup(mxname);
        }
    }

    return 0;
}

string trim_ref(string &s)
{
    s.erase(s.begin(), find_if(s.begin(), s.end(), not1(ptr_fun<int, int>(isspace))));
    s.erase(find_if(s.rbegin(), s.rend(), not1(ptr_fun<int, int>(isspace))).base(), s.end());
    return s;
}

string trim_val(string s)
{
    s.erase(s.begin(), find_if(s.begin(), s.end(), not1(ptr_fun<int, int>(isspace))));
    s.erase(find_if(s.rbegin(), s.rend(), not1(ptr_fun<int, int>(isspace))).base(), s.end());
    return s;
}
