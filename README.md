#  Project 03 for the University of Tulsa's CYB-3053: Operating Systems Concept course

## Please review the Project handout for full assignment details.

## Additional Instructions

Use the same environment you used for Project 01 and Project 02 (WSL, a Linux VM, Cygwin, other). We will not be building support for Windows.

Ensure that you obtain all files, preferably through a git clone.

Similar to Project 02, a CMakeLists.txt file and a build script have been provided for you. In the build directory, execute "./build.sh", without the quotes. If there are issues with building, ensure that you have execution permissions set (chmod +x build.sh)

## Using the files

### Server

The server must be started before any client can connect to it.

The server can be started from the build directory with "./server", without quotes. There should be no return, and it will appear as if your terminal has hung: this is normal behavior.

To change server defaults, pass "-h" while running the server ("./server -h") to see the list of options. The default port is 10000. 

### Client

For initial testing, it is recommended to utilize the client executable that is built with the build script ("./client"). After you work to complete portions of requests.c, you should be able to use any HTTP client (such as a web browser).

The client requires 3 arguments: the host, the port, and the filepath. The host will be localhost, the port by default should be 10000, and you can use any of the files in the "files" directory (ex: /files/test1.html)

