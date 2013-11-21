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

#include "void.h"

static int indexvoid(lua_State *L){
	int32_t hash=0;
	size_t len;
	link_t *ud,*nd,**sd;
	const uint8_t *str = NULL;
	str = luaL_checklstring(L,2,&len);
	HASH(hash,str,len);
	nd = (link_t *)malloc(sizeof(link_t));
	nd->key = strdup(str);
	nd->hash = hash;
	nd->len = 0;
	nd->count = 1;
	nd->previous = NULL;
	nd->head=NULL;
	nd->tail=&(nd)->head;
	nd->state = NULL;
	nd->waiters=0;
	pthread_mutex_init(&nd->mutex,NULL);
	pthread_cond_init(&nd->cond,NULL);
	sd = &Void.bins[hash & HASH_MASK];
	pthread_mutex_lock(&Void.mutex);
	for(ud=*sd; ud != NULL; ud = ud->next){
		if(ud->hash == hash){
			if(!strcmp(ud->key,str)) break;
		}
	}
	if(ud){
		ud->count++;
		pthread_mutex_unlock(&Void.mutex);
		free(nd->key);
		pthread_mutex_destroy(&nd->mutex);
		pthread_cond_destroy(&nd->cond);
		free(nd);
	}else{
		if(!*sd){
			*sd = nd;
		}else{
			LIST_ADD(*sd,nd);
		}
		pthread_mutex_unlock(&Void.mutex);
		ud=nd;
	}
	link_t **link = (link_t **)lua_newuserdata(L,sizeof(link_t *));
	luaL_getmetatable(L,"void.link");
	lua_setmetatable(L,-2);
	*link = ud;
	return 1;
}

static int indexlink(lua_State *L){
	link_t *link;
	blob_t *blob=NULL;
	void_t *ud;
	int32_t wait;
	LINKCHECK(link);
	int32_t i = (int32_t) lua_tointeger(L,2);
	wait = i<0 ? 1:0;
	i=abs(i);	
	pthread_mutex_lock(&link->mutex);
	if(link->len >= i){
		blob=link->head;
		if(blob){
			if(link->waiters) pthread_cond_broadcast(&link->cond);
			QUEUE_POP(link);
			link->len--;
		}
	}else if(wait){
		link->waiters++;
		while(link->len < i) pthread_cond_wait(&link->cond,&link->mutex);
		link->waiters--;
		blob=link->head;
		QUEUE_POP(link);
		if(link->waiters) pthread_cond_broadcast(&link->cond);
		link->len--;
	}
	pthread_mutex_unlock(&link->mutex);
	if(blob){
		ud = (void_t *) lua_newuserdata(L,sizeof(void_t));
		luaL_getmetatable(L,"void.view");
		lua_setmetatable(L,-2);
		ud->size=blob->size;
		ud->data=blob->data;
		ud->blob=blob;
		ud->type=0;
	}else{
		lua_pushnil(L);
	}
	return 1;
}

static int nindexlink(lua_State *L){
	link_t *link;
	blob_t *blob,*bl;
	void_t *ud;
	int32_t wait;
	size_t len;
	char *s;
	LINKCHECK(link);
	int32_t i = (int32_t) lua_tointeger(L,2);
	switch(lua_type(L,3)){
		case LUA_TSTRING: 
			s = (char *)lua_tolstring(L,3,&len);
			if(!len) LUA_ERROR("Empty string not allowed");
			if(len > 2147483647) LUA_ERROR("Size cannot exceed 2GB.");
			blob = (blob_t *) malloc(sizeof(blob_t)+len);
			blob->size = len;
			memcpy(blob->data,s,len);
			ud=NULL;
			break;
		case LUA_TNUMBER: 
			blob = (blob_t *) malloc(sizeof(blob_t)+sizeof(lua_Number));
			*((lua_Number *)blob->data) = lua_tonumber(L,3);
			blob->size = sizeof(lua_Number);
			ud=NULL;
			break;
		case LUA_TNIL: 
			blob=NULL;
			ud=NULL;
			break;
		case LUA_TUSERDATA:
			ud = (void_t *) luaL_checkudata(L,3,"void.view");
			if(!ud->blob) LUA_ERROR("Neutered void.view");
			blob=ud->blob;
			ud->blob=NULL;
			ud->data=NULL;
			ud->size=0;
			break;
		default: luaL_typerror(L,3,"string, userdata, number or nil");
	}
	wait = i>0 ? 0:1;
	i=abs(i);
	pthread_mutex_lock(&link->mutex);
	if(link->len < i){
		if(blob){
			QUEUE_PUSH(link,blob);
			link->state=L;
			blob = NULL;
			link->len++;
			if(link->waiters) pthread_cond_broadcast(&link->cond);
		}
	}else if(wait){
		link->waiters++;
		while(link->len > i) pthread_cond_wait(&link->cond,&link->mutex);
		link->waiters--;
		if(blob){
			QUEUE_PUSH(link,blob);
			blob=NULL;
			link->len++;
			link->state=L;
			if(link->waiters) pthread_cond_broadcast(&link->cond);
		}
	}else{
		if(blob){ 
			QUEUE_PUSH(link,blob);
			link->state=L;
		}else{ 
			link->len--; 
		}
		blob=link->head;
		QUEUE_POP(link);
		if(link->waiters) pthread_cond_broadcast(&link->cond);
	}
	pthread_mutex_unlock(&link->mutex);
	if(blob) 
		if(ud){
			ud->blob=blob;
			ud->data=blob->data;
			ud->size=blob->size;
		}else{
			free(blob);
		}
	return 0;
}


static int calllink(lua_State *L){
	link_t *link;
	blob_t *blob;
	void_t *ud;
	size_t len;
	char *s;
	int fd;
	LINKCHECK(link);
	int32_t wait = (int32_t) (lua_isboolean(L,3)?lua_toboolean(L,3):0);
	switch(lua_type(L,2)){
		case LUA_TSTRING: 
			s = (char *)lua_tolstring(L,3,&len);
			if(!len) LUA_ERROR("Empty string not allowed");
			if(len > 2147483647) LUA_ERROR("Size cannot exceed 2GB.");
			blob = (blob_t *) malloc(sizeof(blob_t)+len);
			blob->size = len;
			memcpy(blob->data,s,len);
			ud=NULL;
			break;
		case LUA_TNUMBER: 
			blob = (blob_t *) malloc(sizeof(blob_t)+sizeof(lua_Number));
			*((lua_Number *)blob->data) = lua_tonumber(L,3);
			blob->size = sizeof(lua_Number);
			ud=NULL;
			break;
		case LUA_TBOOLEAN:
			wait = (int32_t) lua_toboolean(L,2);
			blob=NULL;
			ud=NULL;
			break;
		case LUA_TUSERDATA:
			ud = (void_t *) luaL_checkudata(L,2,"void.view");
			if(!ud->blob) LUA_ERROR("Neutered void.view");
			blob=ud->blob;
			ud->blob=NULL;
			ud->data=NULL;
			ud->size=0;
			break;
		default: luaL_typerror(L,3,"string, userdata, number or nil");
	}
	pthread_mutex_lock(&link->mutex);
	do{
		if(wait){
			link->waiters++;
			while(link->state == L) pthread_cond_wait(&link->cond,&link->mutex);
			link->waiters--;
			len=1;
		}else if(link->state == L){
			len=0;
		}
		if(blob){ 
			QUEUE_PUSH(link,blob);
			if(blob!=link->head){
				blob=link->head;
				QUEUE_POP(link);
			}else{
				blob=NULL;
				link->len++;
			}
			link->state=L;
			if(link->waiters) 
				pthread_cond_broadcast(&link->cond);
		}
	}while(0);
	pthread_mutex_unlock(&link->mutex);
	if(blob){ 
		if(ud){
		 ud->blob=blob;
		 ud->data=blob->data;
		 ud->size=blob->size;
		}else{
			free(blob);
		}
	}
	lua_pushboolean(L,len);
	return 1;
}

static int gclink(lua_State *L){
	link_t *link,**sp;
	int32_t release=0,error=0;
	LINKCHECK(link);
	lua_pushlstring(L,"__gc",4);
	lua_rawget(L,lua_upvalueindex(1));
	if(lua_isfunction(L,-1)){
		lua_pushvalue(L,1);
		if(lua_pcall(L,1,0,0)){
			error=1;
		}
	}
	pthread_mutex_lock(&link->mutex);
	link->count--;
	sp = &Void.bins[link->hash & HASH_MASK];
	if(!link->count && !link->len){
		release=1;
		pthread_mutex_lock(&Void.mutex);
		if(link == *sp){
			*sp = link->next;
		}else{
			LIST_REMOVE(link);
		}
		pthread_mutex_unlock(&Void.mutex);
	}
	pthread_mutex_unlock(&link->mutex);	
	if(release){
		free(link->key);
		pthread_mutex_destroy(&link->mutex);
		pthread_cond_destroy(&link->cond);
		free(link);
	}
	if(error){
		lua_error(L);
	}
	return 0;
}

static int lenlink(lua_State *L){
	link_t *link;
	int32_t len;
	LINKCHECK(link);
	pthread_mutex_lock(&link->mutex);
	len = link->len;
	pthread_mutex_unlock(&link->mutex);	
	lua_settop(L,0);
	lua_pushinteger(L,len);
	return 1;
}

static int printlink(lua_State *L){
	link_t *link;
	LINKCHECK(link);
	lua_pushfstring(L,"void.link: %p",(void *)link);
	return 1;
}

static int printview(lua_State *L){
	void_t *ud;
	VIEWCHECK(ud,1);
	lua_pushfstring(L,"void.view: %p  %p",(void *)ud,(void *)ud->blob);
	return 1;
}

static int callvoid(lua_State *L){
	void_t *ud=NULL;
	blob_t *old = NULL;
	uint32_t size;
	if(lua_isuserdata(L,3)){
		ud = luaL_checkudata(L,3,"void.view");
		if(ud->blob) old=ud->blob;
		size = (uint32_t)luaL_optinteger(L,2,0);
		lua_pushvalue(L,3);
	}else{
		size = (uint32_t)luaL_optinteger(L,2,0);
		ud = (void_t *) lua_newuserdata(L,sizeof(void_t));
		luaL_getmetatable(L,"void.view");
		lua_setmetatable(L,-2);
		ud->type=0;
		old=NULL;
	}
	if(size > 2147483647) LUA_ERROR("size cannot exceed 2GB");
	ud->blob = size ? (blob_t *) realloc(old,sizeof(blob_t)+size):NULL;
	ud->data = ud->blob ? ud->blob->data:NULL;
	ud->blob->size = ud->blob ? size:0;
	ud->size = ud->blob->size;
	return 1;
}

static int lenview(lua_State *L){
	void_t *ud;
	ud = (void_t *) lua_touserdata(L,1);
	if(!ud->blob){
		lua_pushnil(L);
	}else{
		switch(ud->type){
			case VOID_TYPE_U8:  lua_pushinteger(L,(lua_Integer) ud->size);   break;
			case VOID_TYPE_S8:  lua_pushinteger(L,(lua_Integer) ud->size);   break;
			case VOID_TYPE_U16: lua_pushinteger(L,(lua_Integer) ud->size>>1);   break;
			case VOID_TYPE_S16: lua_pushinteger(L,(lua_Integer) ud->size>>1);   break;
			case VOID_TYPE_U32: lua_pushinteger(L,(lua_Integer) ud->size>>2);   break;
			case VOID_TYPE_S32: lua_pushinteger(L,(lua_Integer) ud->size>>2);   break;
			case VOID_TYPE_FLOAT: lua_pushinteger(L,(lua_Integer) ud->size>>2);   break;
			case VOID_TYPE_DOUBLE: lua_pushinteger(L,(lua_Integer) ud->size>>3);   break;
		}
	}
	return 1;
}

SFUNC("__unm",unmview,5);
DFUNC("__add",addview,5);
DFUNC("__sub",subview,5);
DFUNC("__mul",mulview,5);
DFUNC("__div",divview,5);
DFUNC("__mod",modview,5);
DFUNC("__pow",powview,5);
DFUNC("__concat",concatview,8);
DFUNC("__eq",eqview,4);
DFUNC("__lt",ltview,4);
DFUNC("__le",leview,4);

static int gcview(lua_State *L){
	void_t *ud;
	int error=0;
	VIEWCHECK(ud,1);
	lua_pushlstring(L,"__gc",4);
	lua_rawget(L,lua_upvalueindex(1));
	if(lua_isfunction(L,-1)){
		lua_pushvalue(L,1);
		if(lua_pcall(L,1,0,0)){
			error=1;
		}
	}
	if(ud->blob){ FREEBLOB(ud);}
	lua_pushvalue(L,1);
	lua_rawget(L,lua_upvalueindex(1));
	if(lua_type(L,-1)!=LUA_TNIL){
		lua_pushvalue(L,1);
		lua_pushnil(L);
		lua_rawset(L,lua_upvalueindex(1));	
	}
	if(error) lua_error(L);
	return 0;
}

static int indexview(lua_State *L){
	void_t *ud;
	size_t index;
	VIEWCHECK(ud,1);
	index = (size_t)lua_tointeger(L, 2);
	if(index){
		index--;
		switch(ud->type){
			case VOID_TYPE_U8:  if(index < ud->size) lua_pushinteger(L,(lua_Integer) ((uint8_t *)(ud->data))[index]);  else return 0; break;
			case VOID_TYPE_S8:  if(index < ud->size) lua_pushinteger(L,(lua_Integer) ((int8_t *)(ud->data))[index]);  else return 0; break;
			case VOID_TYPE_U16: if(index < ud->size >> 1) lua_pushinteger(L,(lua_Integer) ((uint16_t *)(ud->data))[index]); else return 0;  break;
			case VOID_TYPE_S16: if(index < ud->size >> 1) lua_pushinteger(L,(lua_Integer) ((int16_t *)(ud->data))[index]); else return 0;  break;
			case VOID_TYPE_U32: if(index < ud->size >> 2) lua_pushnumber(L,(lua_Number) ((uint32_t *)(ud->data))[index]); else return 0;  break;
			case VOID_TYPE_S32: if(index < ud->size >> 2) lua_pushinteger(L,(lua_Integer) ((int32_t *)(ud->data))[index]);  else return 0; break;
			case VOID_TYPE_FLOAT: if(index < ud->size >> 2) lua_pushnumber(L,(lua_Number) ((float *)(ud->data))[index]); else return 0;  break;
			case VOID_TYPE_DOUBLE: if(index < ud->size >> 3) lua_pushnumber(L,(lua_Number) ((double *)(ud->data))[index]);  else return 0; break;
		}
	}else{
		size_t len;
		const char *str;
		switch(lua_type(L,2)){
			case LUA_TNUMBER:
				lua_pushlightuserdata(L,(void *) ud->data);
				return 1;
			case LUA_TSTRING:
				str = lua_tolstring(L,2,&len);
				if(len==4){
					if(*((int32_t *)str) == *((int32_t *)"from")){
						lua_pushinteger(L,(lua_Integer) (ud->data-ud->blob->data)+1);
						return 1;
					}else if(*((int32_t *)str) == *((int32_t *)"size")){
						lua_pushinteger(L,(lua_Integer) ud->size);
						return 1;
					}else if(*((int32_t *)str) == *((int32_t *)"this")){
						lua_pop(L,1);
						lua_rawget(L,lua_upvalueindex(1));
						return 1;
					}else if(*((int32_t *)str) == *((int32_t *)"type")){
						lua_pushlstring(L,types[ud->type],strlen(types[ud->type]));
						return 1;
					}else if(*((int32_t *)str) == *((int32_t *)"blob")){
						lua_pushinteger(L,(lua_Integer) ud->blob->size);
						return 1;
					}
				}
		}
		lua_rawget(L,lua_upvalueindex(1));
	}
	return 1;
}

static int nindexview(lua_State *L){
	void_t *ud,*vd;
	int32_t i, len;
	size_t index;
	union {uint8_t u8;int8_t s8;uint16_t u16;int16_t s16;uint32_t u32;int32_t s32;float fl;double dl;} u;
	unsigned const char *str;
	index  = (size_t) lua_tointeger(L,2);
	VIEWCHECK(ud,1);
	if(index){
		index--;
		switch(ud->type){
			case VOID_TYPE_U8:  if(index < ud->size)  ((uint8_t *)(ud->data))[index] = (uint8_t)lua_tointeger(L,3); else LUA_ERROR("invalid index"); break;
			case VOID_TYPE_S8:  if(index < ud->size)  ((int8_t *)(ud->data))[index] = (int8_t)lua_tointeger(L,3); else LUA_ERROR("invalid index"); break;
			case VOID_TYPE_U16: if(index < ud->size >> 1)  ((uint16_t *)(ud->data))[index] = (uint16_t)lua_tointeger(L,3); else LUA_ERROR("invalid index"); break;
			case VOID_TYPE_S16: if(index < ud->size >> 1)  ((int16_t *)(ud->data))[index] = (int16_t)lua_tointeger(L,3); else LUA_ERROR("invalid index"); break;
			case VOID_TYPE_U32: if(index < ud->size >> 2)  ((uint32_t *)(ud->data))[index] = (uint32_t)lua_tointeger(L,3); else LUA_ERROR("invalid index"); break;
			case VOID_TYPE_S32: if(index < ud->size >> 2)  ((int32_t *)(ud->data))[index] = (int32_t)lua_tointeger(L,3); else LUA_ERROR("invalid index"); break;
			case VOID_TYPE_FLOAT: if(index < ud->size >> 2)  ((float *)(ud->data))[index] = (float)lua_tonumber(L,3); else LUA_ERROR("invalid index"); break;
			case VOID_TYPE_DOUBLE: if(index < ud->size >> 3)  ((double *)(ud->data))[index] = (double)lua_tonumber(L,3); else LUA_ERROR("invalid index"); break;
		}
	}else{
		size_t len;
		const char *str;
		switch(lua_type(L,2)){
			case LUA_TNUMBER:
				switch(lua_type(L,3)){
					case LUA_TSTRING: 
						str = lua_tolstring(L,3,&len);
						if(len){
							len = MIN(ud->size,len);
							memcpy(ud->data,str,len);
						}
						break;
					case LUA_TUSERDATA: 
						vd = (void_t *)luaL_checkudata(L,3,"void.view");
						luaL_argcheck(L,ud->data,3,"Neutered view");
						len = MIN(ud->size,vd->size);
						memcpy(ud->data,vd->data,len);
						break;
					case LUA_TNUMBER:
						len=0;
						switch(ud->type){
							case VOID_TYPE_U8:  u.u8 = (uint8_t)lua_tointeger(L,3); while(len < ud->size){  *((uint8_t *)(ud->data+len))  = u.u8; len++;} break;
							case VOID_TYPE_S8:  u.s8 = (int8_t)lua_tointeger(L,3); while(len < ud->size){  *((int8_t *)(ud->data+len))  = u.s8; len++;} break;
							case VOID_TYPE_U16: u.u16 = (uint16_t)lua_tointeger(L,3); while(len < ud->size){  *((uint16_t *)(ud->data+len))  = u.u16; len += 2;} break;
							case VOID_TYPE_S16: u.s16 = (int16_t)lua_tointeger(L,3); while(len < ud->size){  *((int16_t *)(ud->data+len))  = u.s16; len+=2;} break;
							case VOID_TYPE_U32: u.u32 = (uint32_t)lua_tonumber(L,3); while(len < ud->size){  *((uint32_t *)(ud->data+len))  = u.u32; len+=4;} break;
							case VOID_TYPE_S32: u.s32 = (int32_t)lua_tointeger(L,3); while(len < ud->size){  *((int32_t *)(ud->data+len))  = u.s32; len+=4;} break;
							case VOID_TYPE_FLOAT: u.fl = (float)lua_tonumber(L,3); while(len < ud->size){  *((float *)(ud->data+len))  = u.fl; len+=4;} break;
							case VOID_TYPE_DOUBLE: u.dl = (double)lua_tonumber(L,3); while(len < ud->size){  *((double *)(ud->data+len))  = u.dl; len+=8;} break;
						}
						break;
				}
				return 0;
			case LUA_TSTRING:
				str = lua_tolstring(L,2,&len);
				if(len == 4){
					if(*((int32_t *)str) == *((int32_t *)"from")){
						i=luaL_checkint(L,3); 
						if(--i < ud->blob->size) 
							ud->data = ud->blob->data+i;
						else
							luaL_argerror(L,3,"out of bounds");
						if(ud->data-ud->blob->data + ud->size > ud->blob->size) 
							ud->size = ud->blob->size - (ud->data-ud->blob->data);
						return 0;
					}else if(*((int32_t *)str) == *((int32_t *)"size")){
						if((i=luaL_optint(L,3,0)) <= ud->blob->size) 
							ud->size=i; 
						return 0;
					}else if(*((int32_t *)str) == *((int32_t *)"this")){
						lua_pushvalue(L,1);
						lua_pushvalue(L,3);
						lua_rawset(L,lua_upvalueindex(1));
						return 0;
					}else if(*((int32_t *)str) == *((int32_t *)"type")){
						ud->type=luaL_checkoption(L,3,NULL,types);
						return 0;
					}
				}
		}
		LUA_ERROR("invalid index");
	}
	return 0;
}

static int callview(lua_State *L){
	void_t *ud,*vd;
	char *s,*e;
	size_t len;
	size_t i,j,k;
	VIEWCHECK(ud,1);
	switch(lua_gettop(L)){
		case 1: lua_pushlstring(L,(const char *)ud->data,ud->size); return 1;
		case 2: i = lua_tointeger(L,2); if(!i) luaL_argerror(L,2,"invalid argument"); j=1;break;
		case 3: 
			i = lua_tointeger(L,2); 
			if(!i) luaL_argerror(L,2,"invalid argument"); 
			j = lua_tointeger(L,3); 
			if(!j) luaL_argerror(L,3,"invalid argument"); 
			break;
	}
	i--;
	luaL_checkstack(L,j,"too many elements requested");
	switch(ud->type){
		case VOID_TYPE_U8: MULTIREAD(uint8_t,1,0,integer,lua_Integer); break; 
		case VOID_TYPE_S8: MULTIREAD(int8_t,1,0,integer,lua_Integer); break; 
		case VOID_TYPE_U16: MULTIREAD(uint16_t,2,1,integer,lua_Integer); break; 
		case VOID_TYPE_S16: MULTIREAD(int16_t,2,1,integer,lua_Integer); break; 
		case VOID_TYPE_U32: MULTIREAD(uint32_t,4,2,number,lua_Number); break; 
		case VOID_TYPE_S32: MULTIREAD(int32_t,4,2,integer,lua_Integer); break; 
		case VOID_TYPE_FLOAT: MULTIREAD(float,4,2,number,lua_Number); break; 
		case VOID_TYPE_DOUBLE: MULTIREAD(double,8,3,number,lua_Number); break; 
	}
	return j;
}

static int readview(lua_State *L){
	void_t *ud = (void_t *)luaL_checkudata(L,1,"void.view");
	luaL_argcheck(L,ud->data,1,"Neutered view");
	FILE *fp;
	int ret;
	switch(lua_type(L,2)){
		case LUA_TNUMBER:
			ret = RECV(((SOCKET)luaL_checkinteger(L,2)),ud->data,ud->size);
			break;
		case LUA_TUSERDATA:
			fp = *(FILE**) luaL_checkudata(L,2,LUA_FILEHANDLE);
			ret = fread((void *)ud->data,1,ud->size,fp);
			break;
		default: luaL_argerror(L,2,"invalid argument");
	}
	if(ret == SOCKET_ERROR){
		lua_pushnil(L);
		lua_pushinteger(L,ERRNO);
		return 2;
	}else{
		lua_pushinteger(L,ret);
		return 1;
	}
}

static int writeview(lua_State *L){
	void_t *ud = (void_t *)luaL_checkudata(L,1,"void.view");
	luaL_argcheck(L,ud->data,1,"Neutered view");
	FILE *fp;
	int ret;
	switch(lua_type(L,2)){
		case LUA_TNUMBER:
			ret = SEND(((SOCKET)luaL_checkinteger(L,2)),ud->data,ud->size);
			break;
		case LUA_TUSERDATA:
			fp = *(FILE**) luaL_checkudata(L,2,LUA_FILEHANDLE);
			ret = fwrite((void *)ud->data,1,ud->size,fp);
			break;
		default: luaL_argerror(L,2,"invalid argument");
	}
	if(ret == SOCKET_ERROR){
		lua_pushnil(L);
		lua_pushinteger(L,ERRNO);
		return 2;
	}else{
		lua_pushinteger(L,ret);
		return 1;
	}
}

static int findview(lua_State *L){
	void_t *ud = (void_t *)luaL_checkudata(L,1,"void.view");
	luaL_argcheck(L,ud->data,1,"Neutered view");
	FILE *fp;
	int ret;
	size_t len;
	char c,*found;
	const char *str = lua_tolstring(L,2,&len);
	if(str && len){
		c = ud->data[ud->size];
		ud->data[ud->size] = 0;
		found = strstr((const char *)ud->data,str);
		if(found){
			lua_pushinteger(L,(lua_Integer) (found - ((char *)ud->data))+1);
			lua_pushinteger(L,(lua_Integer) (found - ((char *)ud->blob->data))+1);
			return 2;
		}else{
			lua_pushnil(L);
		}
		ud->data[ud->size] = c;
	}else{
		lua_pushnil(L);
	}
	return 1;
}

static const struct luaL_reg voidmethods [] = {
	{"__index", indexvoid},
	{"__call",callvoid},
	{NULL, NULL}
};

static const struct luaL_reg viewmethods [] = {
	{"__len", lenview},
	{"__call",callview},
	{"__tostring", printview},
	{NULL, NULL}
};

static const struct luaL_reg moremethods [] = {
	{"read", readview},
	{"write", writeview},
	{"find", findview},
	{NULL, NULL}
};

static const struct luaL_reg linkmethods [] = {
	{"__index", indexlink},
	{"__newindex", nindexlink},
	{"__len",lenlink},
	{"__tostring",printlink},
	{"__call",calllink},
	{NULL, NULL}
};
    
int32_t LUA_API luaopen_void (lua_State *L) {
	lua_newtable(L);
	luaL_openlib(L, NULL, moremethods, 0);
	luaL_newmetatable(L,"void.view");
	luaL_openlib(L, NULL, viewmethods, 0);
	lua_pushstring(L, "__metatable");
	lua_pushvalue(L,-3);
	lua_settable(L, -3);
	VCLOSURE("__index",indexview);
	VCLOSURE("__newindex",nindexview);
	VCLOSURE("__gc",gcview);
	VCLOSURE("__unm",unmview);
	VCLOSURE("__add",addview);
	VCLOSURE("__sub",subview);
	VCLOSURE("__mul",mulview);
	VCLOSURE("__div",divview);
	VCLOSURE("__mod",modview);
	VCLOSURE("__pow",powview);
	VCLOSURE("__concat",concatview);
	VCLOSURE("__eq",eqview);
	VCLOSURE("__lt",ltview);
	VCLOSURE("__le",leview);
	lua_newtable(L);
	luaL_newmetatable(L,"void.link");
	luaL_openlib(L, NULL, linkmethods, 0);
	lua_pushstring(L, "__metatable");
	lua_pushvalue(L,-3);
	lua_settable(L, -3);
	VCLOSURE("__gc",gclink);
	lua_newtable(L);
	lua_createtable(L,0,0); 
	luaL_openlib(L, NULL, voidmethods, 0);
	lua_pushstring(L, "__metatable");
	lua_pushvalue(L,-7);
	lua_settable(L, -3);
	lua_setmetatable(L,-2);
	return 1;
}
