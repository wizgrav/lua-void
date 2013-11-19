/*
===============================================================================

Copyright (C) 2013 Yannis Gravezas <wizgrav@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

===============================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if defined _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef CRITICAL_SECTION pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {(void *)-1,-1,0,0,0,0}
#define pthread_mutex_init(m,a) InitializeCriticalSection(m)
#define pthread_mutex_destroy(m) DeleteCriticalSection(m)
#define pthread_mutex_lock(m) EnterCriticalSection(m)
#define pthread_mutex_unlock(m) LeaveCriticalSection(m)
typedef CONDITION_VARIABLE pthread_cond_t;
#define PTHREAD_COND_INITIALIZER {0)
#define pthread_cond_init(c,a) InitializeConditionVariable(c)
#define pthread_cond_destroy(c) (void)c
#define pthread_cond_wait(c,m) SleepConditionVariableCS(c,m,INFINITE)
#define pthread_cond_broadcast(c) WakeAllConditionVariable(c)
#define SEND(s,d,l) send(s,d,l,0)
#define RECV(s,d,l) recv(s,d,l,0)
#define ERRNO WSAGetLastError()
#else
#include <pthread.h>
#include <errno.h>
#define SOCKET int
#define SOCKET_ERROR -1
#define SEND(s,d,l) write(s,d,l)
#define RECV(s,d,l) read(s,d,l)
#define ERRNO errno
#endif

#if defined __linux
#include <sys/eventfd.h>
#endif

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#ifndef HASH_BINS
#define HASH_BINS 256
#define HASH_MASK 255
#endif


#define LIST_REMOVE(item)\
	if ((item)->next != NULL) (item)->next->previous = (item)->previous;\
	*(item)->previous = (item)->next
#define LIST_ADD(head,item)\
	if(((item)->next = (head)->next) != NULL) (head)->next->previous = &(item)->next;\
	(head)->next = (item);\
	(item)->previous = &(head)->next
#define QUEUE_PUSH(head, item)\
	(item)->next = NULL;\
	*(head)->tail = (item);\
	(head)->tail = &(item)->next
#define QUEUE_POP(start)\
	if (((start)->head = (start)->head->next) == NULL) (start)->tail = &(start)->head
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#define LUA_ERROR(error) luaL_error(L,error)
#define HASH(hash,key,len)\
  while(len--)\
    hash = hash << 5 + hash + key[len]
#define VIEWCHECK(ud,idx) \
	ud = (void_t *) lua_touserdata(L,idx);\
	if(!ud->blob) LUA_ERROR("Neutered void.view")
#define LINKCHECK(ud) ud = *(link_t **) lua_touserdata(L,1)
#define FREEBLOB(ud)\
	ud->blob->next = NULL;\
	free(ud->blob);\
	ud->blob = NULL
#define FREELINK(ud) if(!(--ud->count) && !ud->len) free(ud)	
#define MULTIREAD(type,incr,shift,unit,lt)\
	k=j<<shift;\
	i=i<<shift;\
	if(ud->size < i + k){ lua_pushnil(L);return 1;}\
	for(s = (char *) (ud->data+i),e = s + k;s != e; s+=incr)\
		lua_push ## unit(L,(lt)*((type *)s))
#define VCLOSURE(name,func)\
	lua_pushstring(L, name);\
	lua_pushvalue(L,-3);\
	lua_pushcclosure(L,&func,1);\
	lua_settable(L, -3)
#define SFUNC(name,func,len)\
	static int func(lua_State *L){\
		lua_pushlstring(L,name,len);\
		lua_rawget(L,lua_upvalueindex(1));\
		lua_pushvalue(L,1);\
		lua_call(L,1,1);\
		return 1;\
	}
#define DFUNC(name,func,len)\
	static int func(lua_State *L){\
		lua_pushlstring(L,name,len);\
		lua_rawget(L,lua_upvalueindex(1));\
		lua_pushvalue(L,1);\
		lua_pushvalue(L,2);\
		lua_call(L,2,1);\
		return 1;\
	}

const char *types[] = {"u8","s8","u16","s16","u32","s32","float","double",NULL};

enum {
	VOID_TYPE_U8 = 0,
	VOID_TYPE_S8 = 1,
	VOID_TYPE_U16 = 2,
	VOID_TYPE_S16 = 3,
	VOID_TYPE_U32 = 4,
	VOID_TYPE_S32 = 5,
	VOID_TYPE_FLOAT = 6,
	VOID_TYPE_DOUBLE = 7
};
	
typedef struct blob_t blob_t;

typedef struct blob_t {
	uint32_t size;
    blob_t *next;
    int8_t data[8];
} blob_t;

typedef struct void_t {
    int32_t type;
    int32_t size;
    int8_t *data;
    blob_t *blob;
} void_t;

typedef struct link_t link_t;

typedef struct link_t{
	uint8_t *key;
    lua_State *state;
    blob_t *head,**tail;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    link_t *next,**previous;
    uint32_t hash,count,len,waiters;
} link_t;

struct {
    pthread_mutex_t mutex;
    link_t *bins[HASH_BINS];
} Void = {PTHREAD_MUTEX_INITIALIZER,{NULL}};
