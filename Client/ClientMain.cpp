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
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#pragma comment(lib, "ws2_32") // ws2_32.lib 링크

#include "clientHeader.h"
#include "resource.h"

/* 윈도우 관련 전역 변수 */
static HINSTANCE g_hInstance; // 프로그램 인스턴스 핸들
static HWND g_hBtnSendFile;  //파일 전송 버튼
static HWND g_hBtnSendMsg; //메시지 전송 버튼
static HWND g_hEditStatus; // 각종 메시지 출력 영역
static HWND g_hBtnErasePic; // 그림 지우기 버튼
static HWND g_hDrawWnd; // 그림을 그릴 윈도우

/* 통신 관련 전역 변수 */
static volatile bool g_isIPv6; // IP 주소
static char g_ipaddr[64]; // 서버 IP 주소[문자열]
static int g_port; // 서버 포트 번호
static volatile bool g_isUDP; // tcp or udp
static HANDLE g_hClientThread; // 스레드 핸들
static volatile bool g_bCommStarted; // 통신 시작 여부
static SOCKET g_sock; // 클라이언트 소켓
static HANDLE g_hReadEvent; // 읽기 이벤트
static HANDLE g_hWriteEvent; // 쓰기(전송)이벤트
static struct sockaddr_in serveraddr4;		// IPv4 서버 소켓 주소 구조체
static struct sockaddr_in6	serveraddr6;	// IPv6 서버 소켓 주소 구조체

/* 메시지 관련 전역 변수 */
static CHAT_MSG g_chatmsg;			//채팅 메시지
static DRAWLINE_MSG g_drawlinemsg;  //선 그리기 메시지
static int g_drawcolor;				//선 그리기 색상
static ERASEPIC_MSG g_erasepicmsg;	//그림 지우기 메시지

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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow) {
	//윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		return 1;
	}

	//이벤트 생성
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWriteEvent == NULL) return 1;

	//전역 변수 초기화(일부)
	g_chatmsg.type = TYPE_CHAT;
	g_drawlinemsg.type = TYPE_DRAWLINE;
	g_drawlinemsg.color = RGB(255, 0, 0);
	g_erasepicmsg.type = TYPE_ERASEPIC;

	//대화상자 생성
	g_hInstance = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	//이벤트 제거
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// 윈속 종료
	WSACleanup();
	return 0;
}

//대화상자 프로시저
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static HWND hChkIsIPv6;
	static HWND hEditIPaddr;
	static HWND hEditPort;
	static HWND hChkIsUDP;
	static HWND hBtnConnect;	
	static HWND hBtnSendFile; //전역 변수에도 저장
	static HWND hBtnSendMsg;; //전역 변수에도 저장
	static HWND hEditMsg;
	static HWND hEditStatus;; //전역 변수에도 저장
	static HWND hColorRed;
	static HWND hColorGreen;
	static HWND hColorBlue;
	static HWND hBtnErasePic; //전역 변수에도 저장
	static HWND hStaticDummy;

	switch (uMsg) {
	case WM_INITDIALOG:
		//컨트롤 핸들 얻기
		hChkIsIPv6 = GetDlgItem(hDlg, IDC_ISIPV6);
		hEditIPaddr = GetDlgItem(hDlg, IDC_IPADDR);
		hEditPort = GetDlgItem(hDlg, IDC_PORT);
		hChkIsUDP = GetDlgItem(hDlg, IDC_ISUDP);
		hBtnConnect = GetDlgItem(hDlg, IDC_CONNECT);
		hBtnSendFile = GetDlgItem(hDlg, IDC_SENDFILE);
		g_hBtnSendFile = hBtnSendFile;
		hBtnSendMsg = GetDlgItem(hDlg, IDC_SENDMSG);
		g_hBtnSendMsg = hBtnSendMsg;
		hEditMsg = GetDlgItem(hDlg, IDC_MSG);
		hEditStatus = GetDlgItem(hDlg, IDC_STATUS);
		g_hEditStatus = hEditStatus;
		hColorRed = GetDlgItem(hDlg, IDC_COLORRED);
		hColorGreen = GetDlgItem(hDlg, IDC_COLORGREEN);
		hColorBlue = GetDlgItem(hDlg, IDC_COLORBLUE);
		hBtnErasePic = GetDlgItem(hDlg, IDC_ERASEPIC);
		g_hBtnErasePic = hBtnErasePic;
		hStaticDummy = GetDlgItem(hDlg, IDC_DUMMY);


		//컨트롤 초기화
		SetDlgItemText(hDlg, IDC_IPADDR, SERVERIP4);
		SetDlgItemInt(hDlg, IDC_PORT, SERVERPORT, FALSE);
		EnableWindow(g_hBtnSendFile, FALSE);
		EnableWindow(g_hBtnSendMsg, FALSE);
		SendMessage(hEditMsg, EM_SETLIMITTEXT, SIZE_DAT / 2, 0);
		SendMessage(hColorRed, BM_SETCHECK, BST_CHECKED, 0);
		SendMessage(hColorGreen, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorBlue, BM_SETCHECK, BST_UNCHECKED, 0);
		EnableWindow(g_hBtnErasePic, FALSE);

		//윈도우 클래스 등록
		WNDCLASS wndclass;
		wndclass.style = CS_HREDRAW | CS_VREDRAW;
		wndclass.lpfnWndProc = ChildWndProc;
		wndclass.cbClsExtra = 0;
		wndclass.cbWndExtra = 0;
		wndclass.hInstance = g_hInstance;
		wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
		wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wndclass.lpszMenuName = NULL;
		wndclass.lpszClassName = _T("MYWndClass");
		if (!RegisterClass(&wndclass)) exit(1);

		//자식 윈도우 생성
		RECT rect; GetWindowRect(hStaticDummy, &rect);
		POINT pt; pt.x = rect.left; pt.y = rect.top;
		ScreenToClient(hDlg, &pt);
		g_hDrawWnd = CreateWindow(_T("MYWndClass"), _T(""), WS_CHILD,
			pt.x, pt.y, rect.right - rect.left, rect.bottom - rect.top,
			hDlg, (HMENU)NULL, g_hInstance, NULL);
		if (g_hDrawWnd == NULL) exit(1);
		ShowWindow(g_hDrawWnd, SW_SHOW);
		UpdateWindow(g_hDrawWnd);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_ISIPV6:
			g_isIPv6 = SendMessage(hChkIsIPv6, BM_GETCHECK, 0, 0);
			if (g_isIPv6 == false)
				SetDlgItemText(hDlg, IDC_IPADDR, SERVERIP4);
			else
				SetDlgItemText(hDlg, IDC_IPADDR, SERVERIP6);
			return TRUE;
		case IDC_CONNECT:
			//컨트롤 상태 얻기
			GetDlgItemTextA(hDlg, IDC_IPADDR, g_ipaddr, sizeof(g_ipaddr));
			g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, TRUE);
			g_isIPv6 = SendMessage(hChkIsIPv6, BM_GETCHECK, 0, 0);
			g_isUDP = SendMessage(hChkIsUDP, BM_GETCHECK, 0, 0);
			//소켓 통신 스레드 시작
			g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
			if (g_hClientThread == NULL) exit(0);
			// 서버 접속 성공 기다림
			while (g_bCommStarted == false);
			// 컨트롤 상태 변경
			EnableWindow(hChkIsIPv6, FALSE);
			EnableWindow(hEditIPaddr, FALSE);
			EnableWindow(hEditPort, FALSE);
			EnableWindow(hChkIsUDP, FALSE);
			EnableWindow(hBtnConnect, FALSE);
			EnableWindow(g_hBtnSendFile, TRUE);
			EnableWindow(g_hBtnSendMsg, TRUE);
			SetFocus(hEditMsg);
			EnableWindow(g_hBtnErasePic, TRUE);
			return TRUE;

		case IDC_SENDFILE:
		{
			OPENFILENAME ofn = { 0 };
			TCHAR szFile[MAX_PATH] = { 0 };
			TCHAR szFilter[] = _T("Text & Image Files(*.txt;*.jpg;*.jpeg;*.png;*.bmp)\0*.txt;*.jpg;*.jpeg;*.png;*.bmp\0All Files(*.*)\0*.*\0");

			ofn.lStructSize = sizeof(OPENFILENAME);
			ofn.hwndOwner = hDlg;
			ofn.lpstrFilter = szFilter;
			ofn.lpstrFile = szFile;
			ofn.nMaxFile = MAX_PATH;
			ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

			if (GetOpenFileName(&ofn)) {
				// 파일 크기 얻기
				HANDLE hFile = CreateFile(szFile, GENERIC_READ, 0, NULL,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (hFile == INVALID_HANDLE_VALUE) {
					MessageBox(hDlg, _T("파일 열기 실패"), _T("오류"), MB_ICONERROR);
					return TRUE;
				}

				DWORD fileSize = GetFileSize(hFile, NULL);
				if (fileSize == INVALID_FILE_SIZE) {
					CloseHandle(hFile);
					MessageBox(hDlg, _T("파일 크기 얻기 실패"), _T("오류"), MB_ICONERROR);
					return TRUE;
				}

				// 파일 데이터 읽기
				char* fileData = (char*)malloc(fileSize);
				DWORD readBytes;
				if (!ReadFile(hFile, fileData, fileSize, &readBytes, NULL)) {
					CloseHandle(hFile);
					free(fileData);
					MessageBox(hDlg, _T("파일 읽기 실패"), _T("오류"), MB_ICONERROR);
					return TRUE;
				}
				CloseHandle(hFile);

				// 파일 메시지 구조체 준비
				FILE_MSG fileMsg;
				fileMsg.type = TYPE_FILE;
				fileMsg.filesize = fileSize;

				// 파일 이름 처리 (Unicode -> ANSI 변환)
				char ansiFilename[MAX_PATH];
				WideCharToMultiByte(CP_ACP, 0, PathFindFileName(szFile), -1,
					ansiFilename, MAX_PATH, NULL, NULL);
				strncpy(fileMsg.filename, ansiFilename, SIZE_DAT / 2 - 1);

				int retval;
				int totalSent = 0, remainSize, sendSize;

				// UDP 전송
				if (g_isUDP) {
					if (!g_isIPv6) { // UDP/IPv4
						// 파일 정보 먼저 전송
						retval = sendto(g_sock, (char*)&fileMsg, SIZE_TOT, 0, (struct sockaddr *)&serveraddr4, sizeof(serveraddr4));
						if (retval == SOCKET_ERROR) {
							MessageBox(hDlg, _T("파일 정보 전송 실패"), _T("오류"), MB_ICONERROR);
							free(fileData);
							return TRUE;
						}

						// 파일 데이터 전송
						while (totalSent < fileSize) {
							remainSize = fileSize - totalSent;
							sendSize = min(remainSize, SIZE_TOT);
							retval = sendto(g_sock, fileData + totalSent, sendSize, 0, (struct sockaddr *)&serveraddr4, sizeof(serveraddr4));
							if (retval == SOCKET_ERROR) {
								MessageBox(hDlg, _T("파일 전송 실패"), _T("오류"), MB_ICONERROR);
								break;
							}
							totalSent += retval;
						}
					}
					else { // UDP/IPv6
						// 파일 정보 먼저 전송
						retval = sendto(g_sock, (char*)&fileMsg, SIZE_TOT, 0,
							(struct sockaddr*)&serveraddr6, sizeof(serveraddr6));
						if (retval == SOCKET_ERROR) {
							MessageBox(hDlg, _T("파일 정보 전송 실패"), _T("오류"), MB_ICONERROR);
							free(fileData);
							return TRUE;
						}

						// 파일 데이터 전송
						while (totalSent < fileSize) {
							remainSize = fileSize - totalSent;
							sendSize = min(remainSize, SIZE_TOT);
							retval = sendto(g_sock, fileData + totalSent, sendSize, 0,
								(struct sockaddr*)&serveraddr6, sizeof(serveraddr6));
							if (retval == SOCKET_ERROR) {
								MessageBox(hDlg, _T("파일 전송 실패"), _T("오류"), MB_ICONERROR);
								break;
							}
							totalSent += retval;
						}
					}
				}
				else { // TCP 전송
					// 파일 정보 먼저 전송
					retval = send(g_sock, (char*)&fileMsg, SIZE_TOT, 0);
					if (retval == SOCKET_ERROR) {
						MessageBox(hDlg, _T("파일 정보 전송 실패"), _T("오류"), MB_ICONERROR);
						free(fileData);
						return TRUE;
					}

					// 파일 데이터 전송
					while (totalSent < fileSize) {
						remainSize = fileSize - totalSent;
						sendSize = min(remainSize, SIZE_TOT);
						retval = send(g_sock, fileData + totalSent, sendSize, 0);
						if (retval == SOCKET_ERROR) {
							MessageBox(hDlg, _T("파일 전송 실패"), _T("오류"), MB_ICONERROR);
							break;
						}
						totalSent += retval;
					}
				}
				free(fileData);
				DisplayText("파일 전송 완료: %s (%d 바이트)\r\n", fileMsg.filename, fileSize);
			}
			return TRUE;
		}
		case IDC_SENDMSG:
			// 이전에 얻은 채팅 메시지 읽기 완료를 기다림
			WaitForSingleObject(g_hReadEvent, INFINITE);
			// 새로운 채팅 메시지를 얻고 쓰기 완료를 알림
			GetDlgItemTextA(hDlg, IDC_MSG, g_chatmsg.msg, SIZE_DAT);
			SetEvent(g_hWriteEvent);
			//입력된 텍스트 전체를 선택 표시
			SendMessage(hEditMsg, EM_SETSEL, 0, -1);
			return TRUE;
		case IDC_COLORRED:
			g_drawlinemsg.color = RGB(255, 0, 0);
			return TRUE;
		case IDC_COLORGREEN:
			g_drawlinemsg.color = RGB(0, 255, 0);
			return TRUE;
		case IDC_COLORBLUE:
			g_drawlinemsg.color = RGB(0, 0, 255);
			return TRUE;
		case IDC_ERASEPIC:
			if (g_isUDP) {
				if (g_isIPv6) sendto(g_sock, (char*)&g_erasepicmsg, SIZE_TOT, 0, (struct sockaddr *)&serveraddr6, sizeof(serveraddr6));
				else sendto(g_sock, (char*)&g_erasepicmsg, SIZE_TOT, 0, (struct sockaddr*)&serveraddr4, sizeof(serveraddr4));
			}
			else {
				// TCP
				send(g_sock, (char*)&g_erasepicmsg, SIZE_TOT, 0);
			}
			return TRUE;
		case IDCANCEL:
			if (g_isUDP) {
				COMM_MSG comm_msg;
				comm_msg.type = TYPE_UDP_DYING;
				if (g_isIPv6) sendto(g_sock, (char*)&comm_msg, SIZE_TOT, 0, (struct sockaddr*)&serveraddr6, sizeof(serveraddr6));
				else sendto(g_sock, (char*)&comm_msg, SIZE_TOT, 0, (struct sockaddr*)&serveraddr4, sizeof(serveraddr4));
			}
			closesocket(g_sock);
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
	}
	return FALSE;
}


//자식 윈도우 프로시저
LRESULT CALLBACK ChildWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	HDC hDC;
	HPEN hPen, hOldPen;
	PAINTSTRUCT ps;
	static int cx, cy;
	static HBITMAP hBitmap;
	static HDC hDCMem;
	static int x0, y0;
	static int x1, y1;
	static bool bDrawing;

	switch (uMsg)
	{
	case WM_SIZE:
		// 화면 출력용 DC  핸들 얻기
		hDC = GetDC(hWnd);
		// 배경 비트맵과 메모리 DC 생성
		cx = LOWORD(lParam);
		cy = HIWORD(lParam);
		hBitmap = CreateCompatibleBitmap(hDC, cx, cy);
		hDCMem = CreateCompatibleDC(hDC);
		SelectObject(hDCMem, hBitmap);
		//배경 비트맵 휜색으로 채움
		SelectObject(hDCMem, GetStockObject(WHITE_BRUSH));
		SelectObject(hDCMem, GetStockObject(WHITE_PEN));
		Rectangle(hDCMem, 0, 0, cx, cy);
		//화면 출력용 DC 핸들 해제
		ReleaseDC(hWnd, hDC);
		return 0;
	case WM_PAINT:
		//화면 출력용 DC 핸들 얻기
		hDC = BeginPaint(hWnd, &ps);
		//배경 비트맵을 화면에 전송
		BitBlt(hDC, 0, 0, cx, cy, hDCMem, 0, 0, SRCCOPY);
		//화면 출력용  DC  핸들 해제
		EndPaint(hWnd, &ps);
		return 0;
	case WM_LBUTTONDOWN:
		//마우스 클릭 좌표 얻기
		x0 = LOWORD(lParam);
		y0 = HIWORD(lParam);
		bDrawing = true;
		return 0;
	case WM_MOUSEMOVE:
		if (bDrawing && g_bCommStarted) {
			//  마우스 클릭 좌표 얻기
			x1 = LOWORD(lParam);
			y1 = HIWORD(lParam);
			// 선 그리기 메시지 보내기
			g_drawlinemsg.x0 = x0;
			g_drawlinemsg.y0 = y0;
			g_drawlinemsg.x1 = x1;
			g_drawlinemsg.y1 = y1;
			if (g_isUDP) {
				if (g_isIPv6) sendto(g_sock, (char*)&g_drawlinemsg, SIZE_TOT, 0, (struct sockaddr *)&serveraddr6, sizeof(serveraddr6));
				else sendto(g_sock, (char*)&g_drawlinemsg, SIZE_TOT, 0, (struct sockaddr*)&serveraddr4, sizeof(serveraddr4));
			}
			else send(g_sock, (char*)&g_drawlinemsg, SIZE_TOT, 0);

			// 마우스 클릭 좌표 갱신
			x0 = x1;
			y0 = y1;
		}
		return 0;
	case WM_LBUTTONUP:
		bDrawing = false;
		return 0;
	case WM_DRAWLINE:
		//화면 출력용 DC와 Pen 핸들 얻기
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, 3, g_drawcolor);
		//윈도우 화면에 1차로 그리기
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
		LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
		SelectObject(hDC, hOldPen);
		// 배경 비트앱에 2차로 그리기
		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDCMem, LOWORD(wParam), HIWORD(wParam), NULL);
		LineTo(hDCMem, LOWORD(lParam), HIWORD(lParam));
		SelectObject(hDCMem, hOldPen);
		//화면 출력용 DC와 Pen 핸들 해제
		DeleteObject(hPen);
		ReleaseDC(hWnd, hDC);
		return 0;
	case WM_ERASEPIC:
		//배경 비트맵 휜색으로 채움
		//배경 비트맵 휜색으로 채움
		SelectObject(hDCMem, GetStockObject(WHITE_BRUSH));
		SelectObject(hDCMem, GetStockObject(WHITE_PEN));
		Rectangle(hDCMem, 0, 0, cx, cy);
		//WM_PAINT 메시지 강제 생성
		InvalidateRect(hWnd, NULL, FALSE);
		return 0;
	case WM_DESTROY:
		DeleteDC(hDCMem);
		DeleteObject(hBitmap);
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// 소켓 통신 스레드 함수(1) - 메인
DWORD WINAPI ClientMain(LPVOID arg) {
	int retval;

	if (g_isIPv6 == false && g_isUDP == false) { // TCP/IPv4 서버
		//socket()
		g_sock = socket(AF_INET, SOCK_STREAM, 0);
		if (g_sock == INVALID_SOCKET) err_quit("socket()");

		//connect()
		struct sockaddr_in serveraddr;
		memset(&serveraddr, 0, sizeof(serveraddr));
		serveraddr.sin_family = AF_INET;
		inet_pton(AF_INET, g_ipaddr, &serveraddr.sin_addr);
		serveraddr.sin_port = htons(g_port);
		retval = connect(g_sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
		if (retval == SOCKET_ERROR) err_quit("connect()");
	}
	else if (g_isIPv6 == true && g_isUDP == false) {// TCP/IPv6 서버
		//소켓 생성
		g_sock = socket(AF_INET6, SOCK_STREAM, 0);
		if (g_sock == INVALID_SOCKET) err_quit("socket()");

		//connect()
		struct sockaddr_in6 serveraddr;
		memset(&serveraddr, 0, sizeof(serveraddr));
		serveraddr.sin6_family = AF_INET6;
		inet_pton(AF_INET6, g_ipaddr, &serveraddr.sin6_addr);
		serveraddr.sin6_port = htons(g_port);
		retval = connect(g_sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
		if (retval == SOCKET_ERROR) err_quit("connect()");
	}
	else if (g_isIPv6 == false && g_isUDP == true) {// UDP/IPv4 서버
		// 소켓 생성
		g_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (g_sock == SOCKET_ERROR) err_quit("socket()");

		memset(&serveraddr4, 0, sizeof(serveraddr4));
		serveraddr4.sin_family = AF_INET;
		inet_pton(AF_INET, g_ipaddr, &serveraddr4.sin_addr);
		serveraddr4.sin_port = htons(g_port);

		retval = sendto(g_sock, "UDP/IPv4 연결 확인", strlen("UDP/IPv4 연결 확인"), 0, (struct sockaddr *)&serveraddr4, sizeof(serveraddr4));
	}
	else if (g_isIPv6 == true && g_isUDP == true) {// UDP/IPv6 서버
		// 소켓 생성
		g_sock = socket(AF_INET6, SOCK_DGRAM, 0);
		if (g_sock == SOCKET_ERROR) err_quit("socket()");

		memset(&serveraddr6, 0, sizeof(serveraddr6));
		serveraddr6.sin6_family = AF_INET6;
		inet_pton(AF_INET6, g_ipaddr, &serveraddr6.sin6_addr);
		serveraddr6.sin6_port = htons(g_port);		

		retval = sendto(g_sock, "UDP/IPv6 연결 확인", strlen("UDP/IPv6 연결 확인"), 0, (struct sockaddr*)&serveraddr6, sizeof(serveraddr6));
	}
	MessageBox(NULL, _T("서버에 접속했습니다."), _T("알림"), MB_ICONINFORMATION);
	// 읽기 쓰기 스레드 생성
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if (hThread[0] == NULL || hThread[1] == NULL) exit(1);
	g_bCommStarted = true;

	// 스레드 종료 대기 (둘 중 하나라도 종료할 때까지
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval += WAIT_OBJECT_0;
	if (retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);
	
	MessageBox(NULL, _T("연결이 끊겼습니다."), _T("알림"), MB_ICONERROR);
	EnableWindow(g_hBtnSendFile, FALSE);
	EnableWindow(g_hBtnSendMsg, FALSE);
	EnableWindow(g_hBtnErasePic, FALSE);
	g_bCommStarted = false;
	closesocket(g_sock);

	return 0;
}

DWORD WINAPI ReadThread(LPVOID arg) {
	int retval;
	COMM_MSG comm_msg;
	CHAT_MSG* chat_msg;
	DRAWLINE_MSG* drawline_msg;
	ERASEPIC_MSG* erasepic_msg;
	FILE_MSG* file_msg;

	// 파일 수신용 변수들
	static HANDLE hFile = INVALID_HANDLE_VALUE;
	static DWORD totalReceived = 0;
	static char fileBuffer[SIZE_TOT];
	int addrlen;
	while (1) {
		if (g_isUDP) {
			if (g_isIPv6) {
				struct sockaddr_in6 peeraddr;
				addrlen = sizeof(peeraddr);
				retval = recvfrom(g_sock, (char*)&comm_msg, SIZE_TOT, 0, (struct sockaddr*)&peeraddr, &addrlen);
				if (retval == SOCKET_ERROR) {
					err_display("recvfrom()");
					break;
				}
			}
			else {
				struct sockaddr_in peeraddr;
				addrlen = sizeof(peeraddr);
				retval = recvfrom(g_sock, (char*)&comm_msg, SIZE_TOT, 0, (struct sockaddr*)&peeraddr, &addrlen);
				if (retval == SOCKET_ERROR) {
					err_display("recvfrom()");
					break;
				}
			}
		}
		else retval = recv(g_sock, (char*)&comm_msg, SIZE_TOT, MSG_WAITALL);
		if (retval == 0 || retval == SOCKET_ERROR) {
			if (retval == SOCKET_ERROR) {
				DisplayText("[오류] 소켓 에러 발생\r\n");
			}
			else {
				DisplayText("[정보] 서버와 연결이 끊어졌습니다.\r\n");
			}
			break;
		}
		if (comm_msg.type == TYPE_CHAT) {
			chat_msg = (CHAT_MSG*)&comm_msg;
			DisplayText("[받은 메시지] %s\r\n", chat_msg->msg);
		}
		else if (comm_msg.type == TYPE_DRAWLINE) {
			drawline_msg = (DRAWLINE_MSG*)&comm_msg;
			g_drawcolor = drawline_msg->color;
			SendMessage(g_hDrawWnd, WM_DRAWLINE,
				MAKEWPARAM(drawline_msg->x0, drawline_msg->y0),
				MAKELPARAM(drawline_msg->x1, drawline_msg->y1));
		}
		else if (comm_msg.type == TYPE_ERASEPIC) {
			erasepic_msg = (ERASEPIC_MSG*)&comm_msg;
			SendMessage(g_hDrawWnd, WM_ERASEPIC, 0, 0);
		}

		else if (comm_msg.type == TYPE_FILE) {
			file_msg = (FILE_MSG*)&comm_msg;

			// 현재 실행 파일의 경로 얻기
			char exePath[MAX_PATH];
			GetModuleFileNameA(NULL, exePath, MAX_PATH);

			// 실행 파일의 디렉토리 경로만 추출
			PathRemoveFileSpecA(exePath);

			char fullFilePath[MAX_PATH];
			PathCombineA(fullFilePath, exePath, file_msg->filename);
			// 파일 생성
			hFile = CreateFileA(fullFilePath,  // fullFilePath 사용
				GENERIC_WRITE,
				0,
				NULL,
				CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				NULL);

			if (hFile == INVALID_HANDLE_VALUE) {
				DisplayText("[오류] 파일 생성 실패: %s\r\n", file_msg->filename);
				continue;
			}

			// 파일 데이터 수신
			totalReceived = 0;
			DWORD remainBytes = file_msg->filesize;
			DisplayText("파일 수신 시작: %s (%d 바이트)\r\n",
				file_msg->filename, file_msg->filesize);

			while (remainBytes > 0) {
				if (g_isUDP) {
					if (!g_isIPv6) { // UDP/IPv4
						struct sockaddr_in peeraddr;
						addrlen = sizeof(peeraddr);
						retval = recvfrom(g_sock, fileBuffer, min(remainBytes, SIZE_TOT), 0,
							(struct sockaddr *)&peeraddr, &addrlen);
					}
					else { // UDP/IPv6
						struct sockaddr_in6 peeraddr;
						addrlen = sizeof(peeraddr);
						retval = recvfrom(g_sock, fileBuffer, min(remainBytes, SIZE_TOT), 0,
							(struct sockaddr *)&peeraddr, &addrlen);
					}
				}
				else {
					retval = recv(g_sock, fileBuffer, min(remainBytes, SIZE_TOT), MSG_WAITALL);
				}
				if (retval == SOCKET_ERROR || retval == 0) {
					DisplayText("[오류] 파일 수신 실패\r\n");
					break;
				}

				DWORD written;
				WriteFile(hFile, fileBuffer, retval, &written, NULL);
				totalReceived += written;
				remainBytes -= retval;
			}

			CloseHandle(hFile);
			hFile = INVALID_HANDLE_VALUE;
			DisplayText("파일 수신 완료! (%d/%d 바이트)\r\n", totalReceived, file_msg->filesize);
		}
		
	}

	// 스레드 종료 전 파일 핸들 정리
	if (hFile != INVALID_HANDLE_VALUE) {
		CloseHandle(hFile);
	}
	return 0;
}

DWORD WINAPI WriteThread(LPVOID	arg) {
	int retval;

	//서버와 통신
	while (1) {
		// 쓰기 완료 기다리기
		WaitForSingleObject(g_hWriteEvent, INFINITE);
		//문자열 길이가 0이면 보내지 않음
		if (strlen(g_chatmsg.msg) == 0) {
			// 메시지 전송 버튼 활성화
			EnableWindow(g_hBtnSendMsg, TRUE);
			//읽기 완료 알리기
			SetEvent(g_hReadEvent);
			continue;
		}
		// 데이터 보내기
		if (g_isUDP) {
			// UDP
			if (g_isIPv6) retval = sendto(g_sock, (char*)&g_chatmsg, SIZE_TOT, 0, (struct sockaddr*)&serveraddr6, sizeof(serveraddr6));
			else retval = sendto(g_sock, (char*)&g_chatmsg, SIZE_TOT, 0, (struct sockaddr*)&serveraddr4, sizeof(serveraddr4));
		}
		else {
			// TCP
			retval = send(g_sock, (char*)&g_chatmsg, SIZE_TOT, 0);
		}
		if (retval == SOCKET_ERROR) {
			err_display("sendto()");
			break;
		}
		// 메시지 전송 버튼 활성화
		EnableWindow(g_hBtnSendMsg, TRUE);
		//읽기 완료 알리기
		SetEvent(g_hReadEvent);
	}
	return 0;
}

//에디트 컨트롤 출력 함수
void DisplayText(const char* fmt, ...) {
	va_list arg;
	va_start(arg, fmt);
	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);
	va_end(arg);

	int nLength = GetWindowTextLength(g_hEditStatus);
	SendMessage(g_hEditStatus, EM_SETSEL, nLength, nLength);
	SendMessageA(g_hEditStatus, EM_REPLACESEL, FALSE, (LPARAM)cbuf);
}
