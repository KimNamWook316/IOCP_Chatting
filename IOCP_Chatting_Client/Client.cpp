#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <process.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define BUF_SIZE 1024
#define NAME_LEN 20 

void ErrorHandling(const char* message);

unsigned WINAPI SendMsg(void* arg);
unsigned WINAPI RecvMsg(void* arg);

char message[BUF_SIZE];
char name[NAME_LEN];

int main(int argc, char* argv[])
{
	WSADATA wsaData;
	SOCKET hSocket;
	SOCKADDR_IN servAdr;
	int strLen, readLen;
	HANDLE hSendThread, hRecvThread;

	if (argc != 4)
	{
		printf("Usage : %s <IP> <PORT> <NAME>\n", argv[0]);
		exit(1);
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartUp() Error");

	hSocket = socket(PF_INET, SOCK_STREAM, 0);

	if (INVALID_SOCKET == hSocket)
		ErrorHandling("socket() Error");

	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;
	servAdr.sin_addr.s_addr = inet_addr(argv[1]);
	servAdr.sin_port = htons(atoi(argv[2]));

	if (SOCKET_ERROR == connect(hSocket, (SOCKADDR*)&servAdr, sizeof(servAdr)))
		ErrorHandling("connect() Error");
	else
	{
		strcpy_s(name, argv[3]);
		printf("Connected As %s...\n", name);
	}

	// Send Thread와 Read Thread를 분리
	hSendThread = (HANDLE)_beginthreadex(NULL, 0, SendMsg, (void*)&hSocket, 0, NULL);
	hRecvThread = (HANDLE)_beginthreadex(NULL, 0, RecvMsg, (void*)&hSocket, 0, NULL);

	WaitForSingleObject(hSendThread, INFINITE);
	WaitForSingleObject(hRecvThread, INFINITE);

	closesocket(hSocket);
	WSACleanup();

	return 0;
}

unsigned WINAPI SendMsg(void* arg)
{
	SOCKET hSock = *((SOCKET*)arg);
	char fullMsg[NAME_LEN + BUF_SIZE];

	while (true)
	{
		fgets(message, BUF_SIZE, stdin);

		if (!strcmp(message, "q\n") || !strcmp(message, "Q\n"))
		{
			closesocket(hSock);
			return 0;
		}

		sprintf_s(fullMsg, "%s : %s", name, message);
		send(hSock, fullMsg, strlen(fullMsg), 0);
	}

	return 0;
}

unsigned WINAPI RecvMsg(void* arg)
{
	SOCKET hSock = *((SOCKET*)arg);
	char fullMsg[NAME_LEN + BUF_SIZE];
	int len;

	while (true)
	{
		len = recv(hSock, fullMsg, NAME_LEN + BUF_SIZE - 1, 0);

		if (-1 == len)
			return -1;

		fullMsg[len] = '\0';
		fputs(fullMsg, stdout);
	}
	return 0;
}

void ErrorHandling(const char* msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}

