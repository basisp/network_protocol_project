/*** 여기서부터 이 책의 모든 예제에서 공통으로 포함하여 사용하는 코드이다. ***/

#define _CRT_SECURE_NO_WARNINGS // 구형 C 함수 사용 시 경고 끄기
#define _WINSOCK_DEPRECATED_NO_WARNINGS // 구형 소켓 API 사용 시 경고 끄기
#define _WINSOCKAPI_ //구버
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <winsock2.h> // 윈속2 메인 헤더
#include <ws2tcpip.h> // 윈속2 확장 헤더

#include <tchar.h> // _T(), ...
#include <stdio.h> // printf(), ...
#include <stdlib.h> // exit(), ...
#include <string.h> // strncpy(), ...

#pragma comment(lib, "ws2_32") // ws2_32.lib 링크

#define SERVERPORT 9000
#define BUFSIZE 256
#define TYPE_FILE 1003 // 기존 메시지 타입들(1000, 1001, 1002)과 구분되는 새로운 타입
#define SIZE_TOT 256  //전송 패킷 고정 256바이트 사용(헤더+데이터)
#define SIZE_DAT (SIZE_TOT - sizeof(int)) //헤더 길이를 제외한 데이터 

typedef struct _FILE_MSG {
    int type;
    char filename[SIZE_DAT / 2];  // 파일명
    DWORD filesize;            // 파일 크기
    char dummy[SIZE_DAT - (SIZE_DAT / 2) - sizeof(DWORD)]; // 남은 공간을 dummy로
} FILE_MSG;

typedef struct _SOCKETINFO
{
    SOCKET sock;
    bool isIpv6;
    bool isUDP;
    char buf[BUFSIZE + 1];
    char addr4[INET_ADDRSTRLEN];
    char addr6[INET6_ADDRSTRLEN];
    int addrlen;
    sockaddr_in clientaddr4;
    sockaddr_in6 clientaddr6;
    bool isReceivingFile;   // 파일 수신 중인지 확인용
    DWORD remainFileSize;   // 남은 파일 크기
} SOCKETINFO;

/* 전역 변수 */
// 소켓 개수
int nTotalSockets = 0;
// 소켓 구조체 배열
SOCKETINFO* SocketInfoArray[FD_SETSIZE];

//소켓 정보 관리 함수
bool AddSocketInfo(SOCKET sock, bool isIPv6, bool isUDP);
void RemoveSocketInfo(int nIndex);
void err_quit(const char* msg);
void err_display(const char* msg);
void err_display(int errcode);
int sendAll(SOCKETINFO* ptr, int retval, char* buf, int j, int addrlen);
int getIndexUDPSocket(struct sockaddr_in*);
int getIndexUDPSocket(struct sockaddr_in6*);

int main(int argc, char* argv[]) {
    int retval;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 1;
    }

    /***TCP - IPv4소켓 생성***/
    SOCKET listen_sock4 = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock4 == INVALID_SOCKET) err_quit("socket()");
    // bind()
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(SERVERPORT);
    retval = bind(listen_sock4, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
    if (retval == SOCKET_ERROR) err_quit("bind(4)");

    // listen()
    retval = listen(listen_sock4, SOMAXCONN);
    if (retval == SOCKET_ERROR) err_quit("listen(4)");
    /***TCP - IPv4소켓 생성 완료***/

    /***TCP - IPv6소켓 생성***/
    SOCKET listen_sock6 = socket(AF_INET6, SOCK_STREAM, 0);
    if (listen_sock6 == INVALID_SOCKET) err_quit("socket()");
    // bind()
    struct sockaddr_in6 serveraddr6;
    memset(&serveraddr6, 0, sizeof(serveraddr6));
    serveraddr6.sin6_family = AF_INET6;
    serveraddr6.sin6_addr = in6addr_any;
    serveraddr6.sin6_port = htons(SERVERPORT);
    retval = bind(listen_sock6, (struct sockaddr*)&serveraddr6, sizeof(serveraddr6));
    if (retval == SOCKET_ERROR) err_quit("bind(6)");
    // listen()
    retval = listen(listen_sock6, SOMAXCONN);
    if (retval == SOCKET_ERROR) err_quit("listen(6)");
    /***TCP - IPv6소켓 생성 완료***/

    /***UDP - IPv4소켓 생성***/
    SOCKET Usock4 = socket(AF_INET, SOCK_DGRAM, 0);
    if (Usock4 == INVALID_SOCKET) err_quit("socket()");
    // bind()
    struct sockaddr_in serveraddr4_UDP;
    memset(&serveraddr4_UDP, 0, sizeof(serveraddr4_UDP));
    serveraddr4_UDP.sin_family = AF_INET;
    serveraddr4_UDP.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr4_UDP.sin_port = htons(SERVERPORT);
    retval = bind(Usock4, (struct sockaddr*)&serveraddr4_UDP, sizeof(serveraddr4_UDP));
    if (retval == SOCKET_ERROR) err_quit("bind(U4)");


    /*---UDP - IPv4소켓 생성 완료---*/


    /***UDP - IPv6소켓 생성***/
    //소켓 생성
    SOCKET Usock6 = socket(AF_INET6, SOCK_DGRAM, 0);
    if (Usock6 == INVALID_SOCKET) err_quit("socket()");
    // bind()
    struct sockaddr_in6 serveraddr6_UDP;
    memset(&serveraddr6_UDP, 0, sizeof(serveraddr6_UDP));
    serveraddr6_UDP.sin6_family = AF_INET6;
    serveraddr6_UDP.sin6_addr = in6addr_any;
    serveraddr6_UDP.sin6_port = htons(SERVERPORT);
    retval = bind(Usock6, (struct sockaddr*)&serveraddr6_UDP, sizeof(serveraddr6_UDP));
    if (retval == SOCKET_ERROR) err_quit("bind(U6)");
    /***UDP - IPv6소켓 생성 완료***/


    // 데이터 통신에 사용할 변수(공통)
    fd_set rset;
    SOCKET client_sock;
    int addrlen;
    //데이터 통신에 사용할 변수(IPv4)
    struct sockaddr_in clientaddr4;
    //데이터 통신에 사용할 변수(IPv6)
    struct sockaddr_in6 clientaddr6;

    while (1) {
        //소켓 셋 초기화
        FD_ZERO(&rset);
        FD_SET(listen_sock4, &rset);
        FD_SET(listen_sock6, &rset);
        FD_SET(Usock4, &rset);
        FD_SET(Usock6, &rset);
        for (int i = 0; i < nTotalSockets; i++) {
            FD_SET(SocketInfoArray[i]->sock, &rset);
        }

        //select()
        retval = select(0, &rset, NULL, NULL, NULL);
        if (retval == SOCKET_ERROR) err_quit("select()");

        //소켓 셋 검사(1-1) : 클라이언트 접속 수용 | TCP IPv4
        if (FD_ISSET(listen_sock4, &rset)) {
            addrlen = sizeof(clientaddr4);
            client_sock = accept(listen_sock4, (struct sockaddr*)&clientaddr4, &addrlen);
            if (client_sock == INVALID_SOCKET) {
                err_display("accept()");
                break;
            }
            else {
                //접속한 클라이언트 정보 출력
                char addr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientaddr4.sin_addr, addr, sizeof(addr));
                printf("\n[TCP/IPv4 서버] 클라이언트 접속: IP 주소 = %s, 포트 번호=%d\n",
                    addr, ntohs(clientaddr4.sin_port));
                if (!AddSocketInfo(client_sock, false, false))
                    closesocket(client_sock);
            }
        }
        //소켓 셋 검사(1-2) : 클라이언트 접속 수용 | TCP IPv6
        if (FD_ISSET(listen_sock6, &rset)) {
            addrlen = sizeof(clientaddr6);
            client_sock = accept(listen_sock6, (struct sockaddr*)&clientaddr6, &addrlen);
            if (client_sock == INVALID_SOCKET) {
                err_display("accept()");
                break;
            }
            else {
                //접속한 클라이언트 정보 출력
                char addr[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &clientaddr6.sin6_addr, addr, sizeof(addr));
                printf("\n[TCP/IPv6 서버] 클라이언트 접속: IP 주소 = %s, 포트 번호=%d\n",
                    addr, ntohs(clientaddr6.sin6_port));
                if (!AddSocketInfo(client_sock, true, false))
                    closesocket(client_sock);
            }
        }
        //소켓 셋 검사(1-3) : 클라이언트 접속 수용 | udp ipv4
        if (FD_ISSET(Usock4, &rset)) {
            char buf[BUFSIZE + 1];
            addrlen = sizeof(clientaddr4);
            retval = recvfrom(Usock4, buf, BUFSIZE, 0,
                (struct sockaddr*)&clientaddr4, &addrlen);
            
            int i = getIndexUDPSocket(&clientaddr4);
            if (i != -1) {
                // 데이터 통신
                SOCKETINFO* ptr = SocketInfoArray[i];
                // UDP
                // 메시지 타입 확인
                int type = *(int*)buf;

                if (type == TYPE_FILE) {  // 파일 전송 시작
                    FILE_MSG* filemsg = (FILE_MSG*)buf;
                    ptr->isReceivingFile = true;
                    ptr->remainFileSize = filemsg->filesize;

                    printf("[파일 전송 시작] %s (%d 바이트)\n",
                        filemsg->filename, filemsg->filesize);

                    // 다른 클라이언트들에게 파일 정보 전송
                    for (int j = 0; j < nTotalSockets; j++) {
                        if (j != i) {  // 송신자는 제외
                            SOCKETINFO* ptr2 = SocketInfoArray[j];
                            if (ptr2->isUDP) {
                                if (ptr2->isIpv6) retval = sendto(ptr2->sock, buf, SIZE_TOT, 0, (struct sockaddr*)&ptr2->clientaddr6, sizeof(ptr2->clientaddr6));
                                else retval = sendto(ptr2->sock, buf, SIZE_TOT, 0, (struct sockaddr*)&ptr2->clientaddr4, sizeof(ptr2->clientaddr4));
                            }
                            else retval = send(ptr2->sock, buf, SIZE_TOT, 0);
                            if (retval == SOCKET_ERROR) {
                                err_display("send()");
                                RemoveSocketInfo(j);
                                --j;
                                continue;
                            }
                        }
                    }
                }
                else if (ptr->isReceivingFile) {  // 파일 데이터 수신 중
                    // 다른 클라이언트들에게 파일 데이터 전송
                    for (int j = 0; j < nTotalSockets; j++) {
                        if (j != i) {  // 송신자는 제외
                            SOCKETINFO* ptr2 = SocketInfoArray[j];
                            if (ptr2->isUDP) {
                                if (ptr2->isIpv6) retval = sendto(ptr2->sock, buf, retval, 0, (struct sockaddr*)&ptr2->clientaddr6, sizeof(ptr2->clientaddr6));
                                else retval = sendto(ptr2->sock, buf, retval, 0, (struct sockaddr*)&ptr2->clientaddr4, sizeof(ptr2->clientaddr4));
                            }
                            else retval = send(ptr2->sock, buf, retval, 0);
                            if (retval == SOCKET_ERROR) {
                                err_display("send()");
                                RemoveSocketInfo(j);
                                --j;
                                continue;
                            }
                        }
                    }
                    ptr->remainFileSize -= retval;
                    if (ptr->remainFileSize <= 0) {
                        ptr->isReceivingFile = false;
                        printf("[파일 전송 완료]\n");
                    }
                }
                else {  // 일반 메시지 처리
                    buf[retval] = '\0';
                    printf("[UDP/IPv4/%s:%d] %s\n", ptr->addr4, ntohs(ptr->clientaddr4.sin_port), buf);

                    // 현재 접속한 모든 클라이언트에 데이터 전송
                    for (int j = 0; j < nTotalSockets; j++) {
                        SOCKETINFO* ptr2 = SocketInfoArray[j];
                        retval = sendAll(ptr2, retval, buf, j, addrlen);
                        if (retval == SOCKET_ERROR) {
                            err_display("send()");
                            RemoveSocketInfo(j);
                            --j;
                            continue;
                        }
                    }
                }
            }
            else {
                if (!AddSocketInfo(Usock4, false, true)) {
                    continue;
                }
                char addr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientaddr4.sin_addr, addr, sizeof(addr));

                SOCKETINFO* ptr = SocketInfoArray[nTotalSockets - 1];
                buf[retval] = '\0';
                strcpy(ptr->buf, buf);
                strcpy(ptr->addr4, addr);
                ptr->clientaddr4 = clientaddr4;
                ptr->addrlen = addrlen;
                printf("[UDP/IPv4/%s:%d] %s\n",
                    ptr->addr4, ntohs(ptr->clientaddr4.sin_port), ptr->buf);
            }
            if (retval == 0 || retval == SOCKET_ERROR) { //일단 데이터를 못받거나 받는 도중 오류가 나면 실제로는 연결 해재같은건 없지만 그룹에서 탈락된것으로 간주(그룹도아니지만)
                RemoveSocketInfo(nTotalSockets - 1);
                continue;
            }

            
        }
        //소켓 셋 검사(1-4) : 클라이언트 접속 수용 | udp ipv6
        if (FD_ISSET(Usock6, &rset)) {
            char buf[BUFSIZE + 1];
            addrlen = sizeof(clientaddr6);
            retval = recvfrom(Usock6, buf, BUFSIZE, 0,
                (struct sockaddr*)&clientaddr6, &addrlen);
            int i = getIndexUDPSocket(&clientaddr6);
            if (i != -1) {
                // 데이터 통신
                SOCKETINFO* ptr = SocketInfoArray[i];
                // UDP
                // 메시지 타입 확인
                int type = *(int*)buf;

                if (type == TYPE_FILE) {  // 파일 전송 시작
                    FILE_MSG* filemsg = (FILE_MSG*)buf;
                    ptr->isReceivingFile = true;
                    ptr->remainFileSize = filemsg->filesize;

                    printf("[파일 전송 시작] %s (%d 바이트)\n",
                        filemsg->filename, filemsg->filesize);

                    // 다른 클라이언트들에게 파일 정보 전송
                    for (int j = 0; j < nTotalSockets; j++) {
                        if (j != i) {  // 송신자는 제외
                            SOCKETINFO* ptr2 = SocketInfoArray[j];
                            if (ptr2->isUDP) {
                                if (ptr2->isIpv6) retval = sendto(ptr2->sock, buf, SIZE_TOT, 0, (struct sockaddr*)&ptr2->clientaddr6, sizeof(ptr2->clientaddr6));
                                else retval = sendto(ptr2->sock, buf, SIZE_TOT, 0, (struct sockaddr*)&ptr2->clientaddr4, sizeof(ptr2->clientaddr4));
                            }
                            else retval = send(ptr2->sock, buf, SIZE_TOT, 0);
                            if (retval == SOCKET_ERROR) {
                                err_display("send()");
                                RemoveSocketInfo(j);
                                --j;
                                continue;
                            }
                        }
                    }
                }
                else if (ptr->isReceivingFile) {  // 파일 데이터 수신 중
                    // 다른 클라이언트들에게 파일 데이터 전송
                    for (int j = 0; j < nTotalSockets; j++) {
                        if (j != i) {  // 송신자는 제외
                            SOCKETINFO* ptr2 = SocketInfoArray[j];
                            if (ptr2->isUDP) {
                                if (ptr2->isIpv6) retval = sendto(ptr2->sock, buf, retval, 0, (struct sockaddr*)&ptr2->clientaddr6, sizeof(ptr2->clientaddr6));
                                else retval = sendto(ptr2->sock, buf, retval, 0, (struct sockaddr*)&ptr2->clientaddr4, sizeof(ptr2->clientaddr4));
                            }
                            else retval = send(ptr2->sock, buf, retval, 0);
                            if (retval == SOCKET_ERROR) {
                                err_display("send()");
                                RemoveSocketInfo(j);
                                --j;
                                continue;
                            }
                        }
                    }
                    ptr->remainFileSize -= retval;
                    if (ptr->remainFileSize <= 0) {
                        ptr->isReceivingFile = false;
                        printf("[파일 전송 완료]\n");
                    }
                }
                else {  // 일반 메시지 처리
                    buf[retval] = '\0';
                    printf("[UDP/IPv6/%s:%d] %s\n", ptr->addr6, ntohs(ptr->clientaddr6.sin6_port), buf);

                    // 현재 접속한 모든 클라이언트에 데이터 전송
                    for (int j = 0; j < nTotalSockets; j++) {
                        SOCKETINFO* ptr2 = SocketInfoArray[j];
                        retval = sendAll(ptr2, retval, buf, j, addrlen);
                        if (retval == SOCKET_ERROR) {
                            err_display("send()");
                            RemoveSocketInfo(j);
                            --j;
                            continue;
                        }
                    }
                }
            }
            else {
                if (!AddSocketInfo(Usock6, true, true)) {
                    continue;
                }
                char addr[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &clientaddr6.sin6_addr, addr, sizeof(addr));

                SOCKETINFO* ptr = SocketInfoArray[nTotalSockets - 1];
                buf[retval] = '\0';
                strcpy(ptr->buf, buf);
                strcpy(ptr->addr6, addr);
                ptr->clientaddr6 = clientaddr6;
                ptr->addrlen = addrlen;
                printf("[UDP/IPv6/%s:%d] %s\n",
                    ptr->addr6, ntohs(clientaddr6.sin6_port), ptr->buf);
            }
            if (retval == 0 || retval == SOCKET_ERROR) { //일단 데이터를 못받거나 받는 도중 오류가 나면 실제로는 연결 해재같은건 없지만 그룹에서 탈락된것으로 간주(그룹도아니지만)
                RemoveSocketInfo(nTotalSockets - 1);
                continue;
            }
        }

        //데이터 통신
        for (int i = 0; i < nTotalSockets; i++) {
            SOCKETINFO* ptr = SocketInfoArray[i];
            if (FD_ISSET(ptr->sock, &rset)) {
                if (ptr->isUDP == false) {
                    // TCP
                    // 데이터 받기
                    retval = recv(ptr->sock, ptr->buf, BUFSIZE, 0);
                    if (retval == 0 || retval == SOCKET_ERROR) {
                        RemoveSocketInfo(i);
                        continue;
                    }

                    // 메시지 타입 확인
                    int type = *(int*)ptr->buf;

                    if (type == TYPE_FILE) {  // 파일 전송 시작
                        FILE_MSG* filemsg = (FILE_MSG*)ptr->buf;
                        ptr->isReceivingFile = true;
                        ptr->remainFileSize = filemsg->filesize;

                        printf("[파일 전송 시작] %s (%d 바이트)\n",
                            filemsg->filename, filemsg->filesize);

                        // 다른 클라이언트들에게 파일 정보 전송
                        for (int j = 0; j < nTotalSockets; j++) {
                            if (j != i) {  // 송신자는 제외
                                SOCKETINFO* ptr2 = SocketInfoArray[j];
                                if (ptr2->isUDP) {
                                    if (ptr2->isIpv6) retval = sendto(ptr2->sock, ptr->buf, SIZE_TOT, 0, (struct sockaddr*)&ptr2->clientaddr6, sizeof(ptr2->clientaddr6));
                                    else retval = sendto(ptr2->sock, ptr->buf, SIZE_TOT, 0, (struct sockaddr*)&ptr2->clientaddr4, sizeof(ptr2->clientaddr4));
                                }
                                else retval = send(ptr2->sock, ptr->buf, SIZE_TOT, 0);
                                
                                if (retval == SOCKET_ERROR) {
                                    err_display("send()");
                                    RemoveSocketInfo(j);
                                    --j;
                                    continue;
                                }
                            }
                        }
                    }
                    else if (ptr->isReceivingFile) {  // 파일 데이터 수신 중
                        // 다른 클라이언트들에게 파일 데이터 전송
                        for (int j = 0; j < nTotalSockets; j++) {
                            if (j != i) {  // 송신자는 제외
                                SOCKETINFO* ptr2 = SocketInfoArray[j];
                                if (ptr2->isUDP) {
                                    if (ptr2->isIpv6) retval = sendto(ptr2->sock, ptr->buf, retval, 0, (struct sockaddr*)&ptr2->clientaddr6, sizeof(ptr2->clientaddr6));
                                    else retval = sendto(ptr2->sock, ptr->buf, retval, 0, (struct sockaddr*)&ptr2->clientaddr4, sizeof(ptr2->clientaddr4));
                                }
                                else retval = send(ptr2->sock, ptr->buf, retval, 0);
                               
                                if (retval == SOCKET_ERROR) {
                                    err_display("send()");
                                    RemoveSocketInfo(j);
                                    --j;
                                    continue;
                                }
                            }
                        }

                        ptr->remainFileSize -= retval;
                        if (ptr->remainFileSize <= 0) {
                            ptr->isReceivingFile = false;
                            printf("[파일 전송 완료]\n");
                        }
                    }
                    else {  // 일반 메시지 처리
                        ptr->buf[retval] = '\0';
                        if (ptr->isIpv6)
                            printf("[TCP/IPv6/%s:%d] %s\n", ptr->addr6, ntohs(ptr->clientaddr6.sin6_port), ptr->buf);
                        else
                            printf("[TCP/IPv4/%s:%d] %s\n", ptr->addr4, ntohs(ptr->clientaddr4.sin_port), ptr->buf);

                        // 현재 접속한 모든 클라이언트에 데이터 전송
                        for (int j = 0; j < nTotalSockets; j++) {
                            SOCKETINFO* ptr2 = SocketInfoArray[j];
                            retval = sendAll(ptr2, retval, ptr->buf, j, addrlen);
                            if (retval == SOCKET_ERROR) {
                                err_display("send()");
                                RemoveSocketInfo(j);
                                --j;
                                continue;
                            }
                        }
                    }
                }
            }
        }
    }
    //소켓 닫기
    closesocket(listen_sock4);
    closesocket(listen_sock6);
    //윈속 종료
    WSACleanup();
    return 0;
}


// 소켓 함수 오류 출력 후 종료
void err_quit(const char* msg)
{
    LPVOID lpMsgBuf;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, WSAGetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char*)&lpMsgBuf, 0, NULL);
    MessageBoxA(NULL, (const char*)lpMsgBuf, msg, MB_ICONERROR);
    LocalFree(lpMsgBuf);
    exit(1);
}

// 소켓 함수 오류 출력
void err_display(const char* msg)
{
    LPVOID lpMsgBuf;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, WSAGetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char*)&lpMsgBuf, 0, NULL);
    printf("[%s] %s\n", msg, (char*)lpMsgBuf);
    LocalFree(lpMsgBuf);
}

// 소켓 함수 오류 출력
void err_display(int errcode)
{
    LPVOID lpMsgBuf;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, errcode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (char*)&lpMsgBuf, 0, NULL);
    printf("[오류] %s\n", (char*)lpMsgBuf);
    LocalFree(lpMsgBuf);
}

bool AddSocketInfo(SOCKET sock, bool isIPv6, bool isUDP) {
    if (nTotalSockets >= FD_SETSIZE) {
        printf("[오류] 소켓 정보를 추가할 수 없습니다!\n");
        return false;
    }

    SOCKETINFO* ptr = new SOCKETINFO();

    if (ptr == NULL) {
        printf("[오류] 메모리가 부족합니다!\n");
        return false;
    }
    ptr->sock = sock;
    ptr->isIpv6 = isIPv6;
    ptr->isUDP = isUDP;
    // 문자열 복사
    if (isIPv6 == false) { //IPv4
        int addrlen = sizeof(struct sockaddr_in);
        getpeername(ptr->sock, (struct sockaddr*)&ptr->clientaddr4, &addrlen);
        inet_ntop(AF_INET, &ptr->clientaddr4.sin_addr, ptr->addr4, sizeof(ptr->addr4));
        printf("소켓 추가 성공, %s:%d\n", ptr->addr4, htons(ptr->clientaddr4.sin_port));

    }
    else { //IPv6
        int addrlen = sizeof(struct sockaddr_in6);
        getpeername(ptr->sock, (struct sockaddr*)&ptr->clientaddr6, &addrlen);
        inet_ntop(AF_INET6, &ptr->clientaddr6.sin6_addr, ptr->addr6, sizeof(ptr->addr6));
    }
    SocketInfoArray[nTotalSockets++] = ptr;
    printf("현재 소켓 개수: %d\n", nTotalSockets);
    return true;
}

void RemoveSocketInfo(int nIndex) {
    SOCKETINFO* ptr = SocketInfoArray[nIndex];

    if (ptr->isIpv6 == false && ptr->isUDP == false) {
        //클라이언트 정보 얻기
        struct sockaddr_in clientaddr;
        int addrlen = sizeof(clientaddr);
        getpeername(ptr->sock, (struct sockaddr*)&clientaddr, &addrlen);
        //클라이언트 정보 출력
        char addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));
        printf("[TCP/IPv4 서버] 클라이언트 종료 : IP 주소=%s, 포트 번호=%d\n", addr, ntohs(clientaddr.sin_port));
    }
    else if (ptr->isIpv6 == true && ptr->isUDP == false) {
        //클라이언트 정보 얻기
        struct sockaddr_in6 clientaddr;
        int addrlen = sizeof(clientaddr);
        getpeername(ptr->sock, (struct sockaddr*)&clientaddr, &addrlen);
        //클라이언트 정보 출력
        char addr[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &clientaddr.sin6_addr, addr, sizeof(addr));
        printf("[TCP/IPv6 서버] 클라이언트 종료 : IP 주소=%s, 포트 번호=%d\n", addr, ntohs(clientaddr.sin6_port));
    }
    else if (ptr->isIpv6 == false && ptr->isUDP == true) {
        //클라이언트 정보 얻기
        struct sockaddr_in clientaddr;
        int addrlen = sizeof(clientaddr);
        getpeername(ptr->sock, (struct sockaddr*)&clientaddr, &addrlen);
        //클라이언트 정보 출력
        char addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));
        printf("[UDP/IPv4 서버] 클라이언트 이상 : IP 주소=%s, 포트 번호=%d\n", addr, ntohs(clientaddr.sin_port));
    }
    else {
        //클라이언트 정보 얻기
        struct sockaddr_in6 clientaddr;
        int addrlen = sizeof(clientaddr);
        getpeername(ptr->sock, (struct sockaddr*)&clientaddr, &addrlen);
        //클라이언트 정보 출력
        char addr[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &clientaddr.sin6_addr, addr, sizeof(addr));
        printf("[UDP/IPv6 서버] 클라이언트 이상 : IP 주소=%s, 포트 번호=%d\n", addr, ntohs(clientaddr.sin6_port));
    }

    //소켓 닫기
    closesocket(ptr->sock);
    delete ptr;
    if (nIndex != (nTotalSockets - 1))
        SocketInfoArray[nIndex] = SocketInfoArray[nTotalSockets - 1];
    --nTotalSockets;
}

int sendAll(SOCKETINFO* ptr, int retval, char* buf, int j, int addrlen) {
    if (ptr->isUDP) {
        // UDP
        if (ptr->isIpv6) {
            // 데이터 보내기
            retval = sendto(ptr->sock, buf, BUFSIZE, 0,
                (struct sockaddr*)&ptr->clientaddr6, addrlen);
            if (retval == SOCKET_ERROR) {
                err_display("sendto()");
                RemoveSocketInfo(j);
            }
        }
        else {
            // 데이터 보내기
            retval = sendto(ptr->sock, buf, BUFSIZE, 0,
                (struct sockaddr*)&ptr->clientaddr4, addrlen);
            if (retval == SOCKET_ERROR) {
                err_display("sendto()");
                RemoveSocketInfo(j);
            }
        }
    }
    else {
        // TCP
        retval = send(ptr->sock, buf, BUFSIZE, 0);
        if (retval == SOCKET_ERROR) {
            err_display("send()");
            RemoveSocketInfo(j);
        }
    }
    return retval;
}

int getIndexUDPSocket(struct sockaddr_in* peeraddr)
{
    for (int i = 0; i < nTotalSockets; i++) {
        SOCKETINFO* ptr = SocketInfoArray[i];
        if (ptr->isUDP) {
            if (ptr->clientaddr4.sin_addr.s_addr == peeraddr->sin_addr.s_addr
                && ptr->clientaddr4.sin_port == peeraddr->sin_port)
                return i;
        }
    }
    return -1;
}

int getIndexUDPSocket(struct sockaddr_in6* peeraddr)
{
    for (int i = 0; i < nTotalSockets; i++) {
        SOCKETINFO* ptr = SocketInfoArray[i];
        if (ptr->isUDP) {
            if (ptr->clientaddr6.sin6_port == peeraddr->sin6_port)
                return i;
        }
    }
    return -1;
}