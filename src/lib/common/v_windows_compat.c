/*
 *
 * ***** BEGIN BSD LICENSE BLOCK *****
 *
 * Copyright (c) 2014, Sean Donnellan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ***** END BSD LICENSE BLOCK *****
 *
 * author: Sean Donnellan <darkdonno@gmail.com>
 *
 */

#include <WinSock2.h>

#include <pthread.h>
#include <semaphore.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "verse.h"

#include "v_common.h"

/* macros/constants */
#define SEM_FAILED ((sem_t *) -1)

/* typedefs */
typedef unsigned int uid_t;

/* function declarations */
char *strndup(const char *s, size_t n);
void usleep(unsigned long ticks);
int gettimeofday(struct timeval *tv, struct timezone *tz);
uid_t geteuid();
sem_t *sem_open_win(const char *name, int oflag, mode_t mode, unsigned int value);
int sem_close_win(sem_t *sem);

/* duplicate a string - by copies at most n bytes. */
char *strndup(const char *s, size_t n) {
	char *result;
	size_t len = strlen(s);
	if (n < len)
		len = n;

	result = (char *)malloc(len + 1);
	if (!result) return 0;

	result[len] = '\0';
	return (char *)memcpy(result, s, len);
}

/* sleeps for a microsecond */
void usleep(unsigned long ticks) {
	LARGE_INTEGER frequency;
	LARGE_INTEGER currentTime;
	LARGE_INTEGER endTime;

	QueryPerformanceCounter(&endTime);
	QueryPerformanceFrequency(&frequency);
	endTime.QuadPart += (ticks * frequency.QuadPart) / (1000ULL * 1000ULL);

	do
	{
		SwitchToThread();
		QueryPerformanceCounter(&currentTime);
	} while (currentTime.QuadPart < endTime.QuadPart);
}

/*
	Get the number of seconds and microseconds since the Epoch.

	POSIX.1-2008 marked gettimeofday() as obsolete and recommends using
	clock_gettime instead.
*/
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	ULARGE_INTEGER  utime;
	ULARGE_INTEGER birthunix;
	FILETIME systemtime;
	/* Windows time starts at 1st of Janurary 1601 where as Unix/POSIX wants 
		January 1, 1970. There are 116444736000000000 * 100ns between them.
	*/
	LONGLONG birthunixhnsec = 116444736000000000;
	LONGLONG microseconds;

	if (tv == NULL) return 1;

	GetSystemTimeAsFileTime(&systemtime);
	utime.LowPart = systemtime.dwLowDateTime;
	utime.HighPart = systemtime.dwHighDateTime;

	birthunix.LowPart = (DWORD)birthunixhnsec;
	birthunix.HighPart = birthunixhnsec >> 32;

	microseconds = (LONGLONG)((utime.QuadPart - birthunix.QuadPart) / 10);

	tv->tv_sec = (long)(microseconds / 1000000);
	tv->tv_usec = (long)(microseconds % 1000000);
	return 0;
}

/* Returns an approximation for the effective user ID of the calling process 

	At the time this was written it was only be used to generate an unique name for a
	semaphore.
*/
uid_t geteuid()
{
	HANDLE handle = NULL;
	PTOKEN_GROUPS ptg = NULL;
	uid_t uid = 0;
	DWORD size = 0;

	if (OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &handle) == FALSE) {
		v_print_log(VRS_PRINT_ERROR, "OpenProcessToken(): failed\n");
		return -1;
	}

	if (!GetTokenInformation(handle, TokenLogonSid, (LPVOID) ptg, 0, &size)) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			v_print_log(VRS_PRINT_ERROR, "GetTokenInformation(): failed to get size\n");
			return 1;
		}
	}

	ptg = (PTOKEN_GROUPS) GlobalAlloc(GPTR, size);

	if (!GetTokenInformation(handle, TokenLogonSid, ptg, size, &size)) {
		v_print_log(VRS_PRINT_ERROR, "GetTokenInformation(): failed\n");
		return 1;
	}

	/*
	Just treat the SID which is made up of a few different bytes as the uid
	It only needs to be good enough to be used as the name of the semaphore.
	*/
	uid = *(uid_t*) (&ptg->Groups[0].Sid);

	HeapFree(GetProcessHeap(), 0, (LPVOID) ptg);
	return uid;
}

/* Provides a getpass equivelent for Windows */
char* getpass(const char* prompt)
{
	/*
	* Decide what prompt to print, if any, and then print it
	*/
	const char* default_prompt = "Password: ";
	if (prompt != NULL) {
		fprintf(stderr, "%s", prompt);
	}
	else if (prompt != "") {
		fprintf(stderr, "%s", default_prompt);
	}

	/*
	* Disable character echoing and line buffering
	*/
	HANDLE standardIn = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode;
	size_t bufferSize = 128;

	if (!GetConsoleMode(standardIn, &mode))
		return NULL;

	if (standardIn == INVALID_HANDLE_VALUE || !(SetConsoleMode(standardIn, 0)))
		return NULL;

	char* buffer = (char*) calloc(bufferSize, 1);
	getchar();
	for (size_t i = 0, c = getchar(); c != '\n' && c != '\r' && c != EOF; ++i, c = getchar()) {
		/*
		* If the buffer gets too full, expand it
		*/
		if (i > bufferSize) {
			bufferSize += 10;
			if (!realloc(buffer, bufferSize))
				return NULL;
		}

		putchar('*');
		buffer[i] = c;
	}

	putchar('\n');

	/*
	* Re-enable character echoing and line buffering
	*/
	if (!SetConsoleMode(standardIn, mode))
		return NULL;
	return buffer;
}

/* Provides a getopt for Windows */
int opterr = 1,   /* if error message should be printed */
    optind = 1,   /* index into parent argv vector */
    optopt,       /* character checked for validity */
    optreset;     /* reset getopt */
char *optarg;    /* argument associated with option */

int getopt(int nargc, char * const nargv[], const char *ostr)
{
	const int badCharacter = (int)'?';
	const int badArgument = (int)':';
	static char *place = "";
	char *oli;

	if (optreset || !*place) {              /* update scanning pointer */
		optreset = 0;
		if (optind >= nargc || *(place = nargv[optind]) != '-') {
			place = "";
			return (-1);
		}
		if (place[1] && *++place == '-') {      /* found "--" */
			++optind;
			place = "";
			return (-1);
		}
	}                                       /* option letter okay? */
	if ((optopt = (int) *place++) == (int)':' ||
		!(oli = strchr(ostr, optopt))) {
		/*
		* if the user didn't specify '-' as an option,
		* assume it means -1.
		*/
		if (optopt == (int)'-')
			return (-1);
		if (!*place)
			++optind;
		if (opterr && *ostr != ':')
			(void)printf("illegal option -- %c\n", optopt);
		return (badCharacter);
	}
	if (*++oli != ':') {                    /* don't need argument */
		optarg = NULL;
		if (!*place)
			++optind;
	} else {
		if (*place) {
			optarg = place;
		} else if (nargc <= ++optind) {
			place = "";
			if (*ostr == ':')
				return badArgument;
			if (opterr)
				(void)printf("option requires an argument -- %c\n", optopt);
			return badCharacter;
		}
		else {
			optarg = nargv[optind];
		}
		place = "";
		++optind;
	}
	return optopt;
}

/* Provides a version of sem_open for Windows that doesn't just return -1 like
	 pthreads-win32 does.
	 
	 This allows named semaphores to be used on Windows.
	*/
sem_t *sem_open_win(const char *name, int oflag, mode_t mode, unsigned int value)
{
	sem_t *result = SEM_FAILED;
	const HANDLE semaphore = CreateSemaphore(NULL, value, SEM_VALUE_MAX, name);
	if (semaphore == NULL)
	{
		printf("CreateSemaphore(): failed: %d\n", GetLastError());
		errno = ENOENT;
		return result;
	}
	return (sem_t*)semaphore;
}

/* Provides a version of sem_close for WIndows that can close the semaphore 
	 opened by sem_open_win.
	*/
int sem_close_win(sem_t * sem)
{
	if (sem) {
		if (!ReleaseSemaphore((HANDLE) sem, 1, NULL)) {
			errno = ENOSYS;
			return -1;
		}
		if (!CloseHandle((HANDLE) sem)) {
			errno = ENOSYS;
			return -1;
		}
		return 0;
	} else {
		errno = ENOSYS;
		return -1;
	}
}
