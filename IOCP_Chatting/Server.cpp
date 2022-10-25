#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <process.h>
#include <list>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

constexpr short bufSize = 1024;
constexpr short nameLen = 20;
constexpr short maxClnt = 1024;

enum class RW_MODE
{
	READ,
	WRITE
};

typedef struct
{
	SOCKET hClntSocket;
	SOCKADDR_IN clntAdr;
} PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

typedef struct
{
	OVERLAPPED overlapped;
	WSABUF wsaBuf;
	char buffer[bufSize];
	RW_MODE rwMode;
	int refCount;
} PER_IO_DATA, *LPPER_IO_DATA;

unsigned int __stdcall IOThreadFunc(void* CompletionIO);
void ErrorHandling(const char* message);

CRITICAL_SECTION crt;
std::list<SOCKET> clientList;

int main(int argc, char* argv[])
{
	WSADATA wsaData;
	HANDLE hComPort;
	SYSTEM_INFO sysInfo;
	LPPER_IO_DATA ioInfo;
	LPPER_HANDLE_DATA handleInfo;

	SOCKET hServSock;
	SOCKADDR_IN servAdr;
	DWORD recvBytes, flags = 0;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartUp() Error");

	InitializeCriticalSection(&crt);

	// Completion Port 오브젝트 생성
	hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	GetSystemInfo(&sysInfo);
	for (int i = 0; i < sysInfo.dwNumberOfProcessors; ++i)
		_beginthreadex(nullptr, 0, IOThreadFunc, static_cast<void*>(hComPort), 0, nullptr);

	hServSock = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;
	servAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAdr.sin_port = htons(atoi(argv[1]));

	bind(hServSock, (SOCKADDR*)&servAdr, sizeof(servAdr));
	listen(hServSock, 5);
	printf("Chatting Server Started\n");

	while (true)
	{
		SOCKET hClntSock;
		SOCKADDR_IN clntAdr;
		int addrLen = sizeof(clntAdr);

		// accept 요청이 있을 때까지 대기
		hClntSock = accept(hServSock, (SOCKADDR*)&clntAdr, &addrLen);
		handleInfo = (LPPER_HANDLE_DATA)malloc(sizeof(PER_HANDLE_DATA));
		handleInfo->hClntSocket = hClntSock;
		memcpy(&(handleInfo->clntAdr), &clntAdr, addrLen);

		// Send할 때 clientList를 순회하므로, 동기화
		EnterCriticalSection(&crt);
		clientList.push_back(hClntSock);
		printf("Conected Client %d\n", clientList.size());
		LeaveCriticalSection(&crt);

		// Completion Port를 소켓과 연결, 핸들 정보가 전달된다.
		CreateIoCompletionPort((HANDLE)hClntSock, hComPort, (DWORD)handleInfo, 0);

		// io 정보 생성하고 Receive
		ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
		memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
		ioInfo->wsaBuf.len = bufSize;
		ioInfo->wsaBuf.buf = ioInfo->buffer;
		ioInfo->rwMode = RW_MODE::READ;

		WSARecv(handleInfo->hClntSocket, &(ioInfo->wsaBuf), 1, &recvBytes, 
			&flags, &(ioInfo->overlapped), NULL);
	}

	DeleteCriticalSection(&crt);

	return 0;
}

unsigned int __stdcall IOThreadFunc(void* CompletionIO)
{
	HANDLE hComPort = (HANDLE)CompletionIO;
	SOCKET sock;
	DWORD byteTrans;
	LPPER_HANDLE_DATA handleInfo;
	LPPER_IO_DATA ioInfo;
	DWORD flags = 0;

	while (true)
	{
		// Completion Queue에서 Overlapped구조체 정보와 handle Info를 받아온다.
		GetQueuedCompletionStatus(hComPort, &byteTrans, (LPDWORD)&handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE);
		sock = handleInfo->hClntSocket;

		if (ioInfo->rwMode == RW_MODE::READ)
		{
			printf("Message Received\n");

			if (byteTrans == 0)
			{
				// 연결이 끊긴 경우, client List에 접근해야 하기 때문에 동기화
				EnterCriticalSection(&crt);
				clientList.erase(std::find(clientList.begin(), clientList.end(), sock));
				printf("Exit Client. Remain Client : %d\n", clientList.size());
				free(handleInfo);
				free(ioInfo);
				LeaveCriticalSection(&crt);
				continue;
			}

			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = byteTrans;

			// client list를 순회하는 도중 list의 원소가 삭제되어서는 안 되기 때문에 동기화
			EnterCriticalSection(&crt);
			ioInfo->rwMode = RW_MODE::WRITE;
			for (auto& e : clientList)
			{
				// 여러 번의 Send요청을 Overlapped IO로 요청하기 때문에, 동일한 주소의 ioInfo를 여러 스레드에서 참조할 수 있다.
				// 따라서 레퍼런스 카운트를 관리해준다.
				++ioInfo->refCount;
				WSASend(e, (&ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
			}
			LeaveCriticalSection(&crt);

			// 전송받은 ioInfo객체는 Send하면서 썼기 때문에 새로 생성하고, 다시 Client의 메시지 Recv
			ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = bufSize;
			ioInfo->wsaBuf.buf = ioInfo->buffer;
			ioInfo->rwMode = RW_MODE::READ;
			WSARecv(sock, &(ioInfo->wsaBuf), 1, NULL, &flags, &(ioInfo->overlapped), NULL);
		}
		else
		{
			--ioInfo->refCount;
			
			// 이 조건에 들어온 스레드가 ioInfo를 참조하는 마지막 스레드이다.
			if (ioInfo->refCount == 0)
			{
				printf("Message Sent To %d Players\n", clientList.size());
				free(ioInfo);
			}
		}
	}
}

void ErrorHandling(const char* msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}
