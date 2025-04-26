#include "win32error.hpp"
#include <WinSock2.h>
#include <MSWSock.h>



class MsWSock {
    public:
        MsWSock() {
            int rc = WSAStartup(MAKEWORD(2,2), &wsadata);
            if (rc != 0) {
                throw std::runtime_error("Failed to initialize winsock: error=" + std::to_string(rc));
            }

            SOCKET sock;
            DWORD dwBytes;


            /* Dummy socket needed for WSAIoctl */
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock == INVALID_SOCKET)
                    throw std::runtime_error("MSWSOCK: failed to create dummy socket");

            {
                GUID guid = WSAID_CONNECTEX;
                rc = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                            &guid, sizeof(guid),
                            &ConnectEx, sizeof(ConnectEx),
                            &dwBytes, NULL, NULL);
                if (rc != 0)
                    throw std::runtime_error("MSWSOCK function ConnectEx is unavailable");
            }

            {
                GUID guid = WSAID_ACCEPTEX;
                rc = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                            &guid, sizeof(guid),
                            &AcceptEx, sizeof(AcceptEx),
                            &dwBytes, NULL, NULL);
                if (rc != 0)
                    throw std::runtime_error("MSWSOCK function AcceptEx is unavailable");
            }

            {
                GUID guid = WSAID_GETACCEPTEXSOCKADDRS;
                rc = WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
                            &guid, sizeof(guid),
                            &GetAcceptExSockaddrs, sizeof(GetAcceptExSockaddrs),
                            &dwBytes, NULL, NULL);
                if (rc != 0)
                    throw std::runtime_error("MSWSOCK function GetAcceptExSockAddrs is unavailable");
            }

            closesocket(sock);

        }
        ~MsWSock() {
            WSACleanup();
        }
        LPFN_CONNECTEX ConnectEx;
        LPFN_ACCEPTEX AcceptEx;
        LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs;

        WSADATA wsadata;
    };
