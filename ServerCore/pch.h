#pragma once

// windows.h에서 사용 빈도가 낮은 API를 제외해 헤더 크기 축소
#define WIN32_LEAN_AND_MEAN

// winsock.h 충돌 방지를 위해 winsock2.h를 windows.h보다 먼저 포함
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")