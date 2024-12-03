#pragma once

#define SERVERIP4 _T("127.0.0.1")
#define SERVERIP6 _T("::1")
#define SERVERPORT 9000

#define SIZE_TOT 256  //전송 패킷 고정 256바이트 사용(헤더+데이터)
#define SIZE_DAT (SIZE_TOT - sizeof(int)) //헤더 길이를 제외한 데이터 
#define MAX_BUFFER SIZE_TOT


#define TYPE_CHAT 1000 // type = 1000 채팅 메시지 
#define TYPE_DRAWLINE 1001 // type = 1001 선 그리기 메시지 
#define TYPE_ERASEPIC 1002 //  type = 1000 그림 지우기 메시지
#define TYPE_FILE 1003 //파일 전송 메세지
#define TYPE_UDP_DYING 1004 // UDP가 죽으면서 내는 소리

#define WM_DRAWLINE (WM_USER + 1)// 사용자 정의 윈도우 메시지 - 선 그리기
#define WM_ERASEPIC (WM_USER + 2)// 사용자 정의 윈도우 메시지 - 그림 지우기
//공통 메시지 형식
//sizeof (COMM_MSG) == 256
typedef struct _COMM_MSG {
	int type;
	char dummy[SIZE_DAT];
} COMM_MSG;

//채팅 메시지 형식
//sizeof(CHAR_MSG) == 256
typedef struct _CHAT_MSG {
	int type;
	char msg[SIZE_DAT];
} CHAT_MSG;

//선 그리기 메시지 형식
//sizeof(DRAWLINE_MSG) == 256
typedef struct _DRAWLINE_MSG {
	int type;
	int color;
	int x0, y0;
	int x1, y1;
	char dummy[SIZE_TOT - 6 * sizeof(int)];
} DRAWLINE_MSG;

//그림 지우기 메시지 형식
//sizeof(ERASEPIC_MSG) == 256
typedef struct _ERASEPIC_MSG {
	int type;
	char dummy[SIZE_DAT];
} ERASEPIC_MSG;

//대화상자 프로시저
INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
//자식윈도우 프로시저
LRESULT CALLBACK ChildWndProc(HWND, UINT, WPARAM, LPARAM);
//소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID	arg);
//에디트 컨트롤 출력 함수
void DisplayText(const char* fmt, ...);

//기본 소켓 에러 검출 함수
void err_quit(const char* msg);
void err_display(const char* msg);
void err_display(int errcode);

typedef struct _FILE_MSG {
	int type;
	char filename[SIZE_DAT / 2];  // 파일명을 위한 공간
	DWORD filesize;             // 파일 크기
	char dummy[SIZE_DAT - (SIZE_DAT / 2) - sizeof(DWORD)]; // 남은 공간을 dummy로
} FILE_MSG;