# Distributed-Software-Update-Framework
A distributed client/server software update system using TCP sockets and POSIX threads. The server listens for client connections, compares software versions, and transfers update packages to outdated clients. Multiple clients are handled simultaneously — each in its own thread — so no client ever waits for another.
