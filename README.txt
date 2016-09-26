Name: Eric Dattore

Instructions and Notes:
Just type 'make' and then run the project1 executable to run the server.
Use telnet to interface with the SMTP server and play around with the.
The server has been tested on Isengard and should Just Work™

I implemented return code 251 for when you are sending emails to non-local individuals.
The mboxes created for @localhost addresses can be read by the Unix mail command (mail -f <filename>).
I also allowed it so you can type in the forward and reverse paths with brackets (<>) or spaces after the command (i.e. MAIL FROM: <..> or MAIL FROM: ...).
I tested the relaying of mail against my Mines account. I couldn't think of another place to test it since most (i.e. Gmail) require TLS and that wasn't a requirement of the assignment.

Citations:
They exist in comments in the code, but the two StackOverflow posts I borrowed code from are:
1. http://stackoverflow.com/questions/504810/how-do-i-find-the-current-machines-full-hostname-in-c-hostname-and-domain-info
2. http://stackoverflow.com/questions/1688432/querying-mx-record-in-c-linux

