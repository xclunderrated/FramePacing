#include <windows.h>
#include <cstdio>
int main() { printf("idle pid=%lu (no presents)\n", GetCurrentProcessId()); for (;;) Sleep(1000); }
