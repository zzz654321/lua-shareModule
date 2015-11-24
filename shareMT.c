#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#if defined(_WIN32 ) 
	#include <windows.h> 
#endif // _WIN32 
#include <dl-lua.tli> 
#include <pthread.h> 

//--------------------------------------------------------------
static pthread_mutex_t MUTEX_S_; 
static lua_State *L_S_= NULL; 
//static pthread_rwlock_t RWLOCK_S_; 

#define LOCKR() pthread_mutex_lock(&MUTEX_S_ ); 
#define LOCKW() LOCKR() 
#define UNLOCK() pthread_mutex_unlock(&MUTEX_S_ ); 

//--------------------------------------------------------------
typedef struct _DUMP_CB { char *p; int l; } DUMP_CB; 
static const char *reader_CB(lua_State *L, 
	void *data, size_t *size ) { 
	DUMP_CB *_ud= (DUMP_CB * )data; *size= (size_t )_ud->l; 
	return (const char * )_ud->p; 
} 
static int writer_CB(lua_State *L, 
	const void *p, size_t sz, void *ud ) { 
	DUMP_CB *_ud= (DUMP_CB * )ud; 
	_ud->p= (char * )realloc(_ud->p, _ud->l+ sz ); 
	memmove(_ud->p+ _ud->l, p, sz ); _ud->l+= sz; 
	return 0; 
} 

static inline void CLONE(lua_State *L, lua_State *, int ); 

//--------------------------------------------------------------
enum //最大支持 TH_MAX 个线程 
	{ TH_MAX= 0x0400, TH_RUNNOT= -2, TH_RUNERR= -1, }; 
typedef struct _TH_STATE 
	{ lua_State *L; pthread_t hTh; } TH_STATE; 
static TH_STATE	*__th_state[TH_MAX ]; 

static void *proxy_ThFunc(void *__ ) { 
	int ret_= 0, i; 
	TH_STATE *th= __th_state[(int )__ ]; 
	//在 L 中执行, 进行数据处理 ... 
	lua_State *L= th->L; //ret_= lua_resume(L, 0 ); 
	ret_= lua_pcall(L, 0, 1, 0 ); 
	//如果执行正确, L 栈高度 <= 1, 否则 > 1 
	i= lua_gettop(L ); if(1< i ) //清理不需要的返回值
		{ lua_replace(L, 1 ); lua_settop(L, 1 ); } 
	if(0!= ret_ ) { lua_pushinteger(L, ret_ ); //错误号 
		lua_pushvalue(L, -2 ); lua_remove(L, 1 ); //进行位置交换 
	} 
	pthread_mutex_lock(&MUTEX_S_ ); 
	__th_state[(int )__ ]= NULL; 
	pthread_mutex_unlock(&MUTEX_S_ ); 
	
	lua_gc(th->L, LUA_GCCOLLECT, 0 ); lua_close(th->L ); 
	pthread_detach(th->hTh ); if(th ) free(th ); 
	
	return NULL; 
} 

static int sth_run(lua_State *L ) { 
	TH_STATE *th= (TH_STATE * )0; 
	unsigned int i= ~0; 
	
	pthread_mutex_lock(&MUTEX_S_ ); for(;;) { 
		if(LUA_TFUNCTION!= lua_type(L, -1 ) ) break; //参数检测 
		for(i= 0; (TH_MAX> i )&& __th_state[i ]; ++i ); 
		if(TH_MAX== i ) break; //没有空间了 
		__th_state[i ]= (TH_STATE * )malloc(sizeof(*th ) ); 
		th= __th_state[i ]; memset(th, 0, sizeof(*th ) ); break; 
	} pthread_mutex_unlock(&MUTEX_S_ ); 
	if(th ) { th->L= lua_open(); luaL_openlibs(th->L ); 
		//lua_getglobal(L, "_G" ); lua_xmove(L, th->L, 1 ); 
		//lua_setglobal(th->L, "_G" ); 
		LOCKW(); CLONE(L, th->L, -1 ); UNLOCK(); 
		//创建线程, 传入参数 
		pthread_create(&(th->hTh ), NULL, 
			proxy_ThFunc, (void * )i ); 
		//lua 返回线程 ID 
		lua_pushinteger(L, i ); return 1; //返回线程 ID 
	} 
	return 0; 
} 

/*/
static int sth_cleanup(lua_State *L ) { 
	for(;;) { int i; if(!lua_isnumber(L, 1 ) ) break; //参数检测 
		i= lua_tointeger(L, 1 ); 
		pthread_mutex_lock(&MUTEX_S_ ); 
		if((TH_MAX> i )&& (0<= i )&& __th_state[i ] ) { 
			//?
		} 
		pthread_mutex_unlock(&MUTEX_S_ ); break; 
	} 
	return 0; 
} //*/

static const char __SMT_CLONE_FUNC[]= 
	"return function(obj ) local lookup_tbl = {} " 
	"	local function _copy(obj ) " 
	"		if type(obj )~= 'table' then return obj end " 
	"		local tbl= lookup_tbl[obj ] if tbl then return tbl end " 
			
	"		tbl= {} lookup_tbl[obj ]= tbl " 
	"		for k, v in pairs(obj ) do " 
	"			tbl[_copy(k ) ]= _copy(v ) end " 
	"		return setmetatable(tbl, getmetatable(obj ) ) " 
	"	end return _copy(obj ) end "; 
static int smt___index(lua_State *L ) { 
	lua_pushvalue(L, -1 ); lua_rawget(L, -3 ); 
	if(!lua_isnil(L, -1 ) ) return 1; lua_pop(L, 1 ); 
	LOCKR(); 
	lua_rawgeti(L_S_, LUA_REGISTRYINDEX, 
		(int )__SMT_CLONE_FUNC ); 
	CLONE(L, L_S_, -1 ); 
	lua_pushvalue(L_S_, -1 ); 
	lua_rawget(L_S_, -3 ); lua_remove(L_S_, -3 ); 
	if(lua_isnil(L_S_, -1 ) ) // 没有找到, 全局中查找 
		{ lua_pop(L_S_, 1 ); lua_rawget(L_S_, LUA_GLOBALSINDEX ); } 
	else lua_remove(L_S_, -2 ); 
	CLONE(L_S_, L, lua_gettop(L_S_ ) ); lua_pop(L_S_, 1 ); 
	UNLOCK(); 
	return 1; 
} 
static int smt___newindex(lua_State *L ) { 
	int i; 
	lua_pushvalue(L, -2 ); lua_rawget(L, -4 ); 
	if(!lua_isnil(L, -1 ) ) { lua_pop(L, 1 ); 
		lua_pushvalue(L, -1 ); lua_rawset(L, -4 ); 
		return 0; 
	} lua_pop(L, 1 ); 
	LOCKW(); 
	lua_rawseti(L_S_, LUA_REGISTRYINDEX, 
		(int )__SMT_CLONE_FUNC ); 
	for(i= -2; -1>= i; ++i ) CLONE(L, L_S_, i ); 
	lua_rawset(L_S_, -3 ); lua_pop(L_S_, 1 ); 
	UNLOCK(); 
	return 0; 
} 

static int sth_dumy(lua_State *L ) { 
	int ret_= 0; const char *str01= "NO UPVALUE!!\n"; 
	ret_= lua_upvalueindex(-1 ); if(lua_isstring(L, ret_ ) ) 
		str01= lua_tolstring(L, ret_, (size_t * )&ret_ ); 
	else ret_= strlen(str01 ); 
	lua_pushlstring(L, str01, ret_ ); ret_= 1; 
	return ret_; 
} 

//--------------------------------------------------------------
enum { UPVALUE_MAX= 0x0100, }; 
static int _cloneRecursion(lua_State *L, const int obj, 
	lua_State *L_, const int r_lv ); 
static inline int _cloneNoTable(lua_State *L, const int idx, 
	lua_State *L_, const int r_lv ) { 
	int ret_= 1; switch(lua_type(L, idx ) ) { 
		case LUA_TSTRING: { 
			size_t l; const char *s= lua_tolstring(L, idx, &l ); 
			lua_pushlstring(L_, s, l ); 
		} break; 
		case LUA_TNUMBER: 
		lua_pushnumber(L_, lua_tonumber(L, idx ) ); 
		break; 
		case LUA_TLIGHTUSERDATA: 
		lua_pushlightuserdata(L_, lua_touserdata(L, idx ) ); 
		break; 
		case LUA_TBOOLEAN: 
		lua_pushboolean(L_, lua_toboolean(L, idx ) ); 
		break; 
		case LUA_TUSERDATA: { // userdata 的 meta 暂时不能拷贝 
			const size_t l= lua_objlen(L, idx ); 
			memmove(lua_newuserdata(L_, l ), 
				lua_touserdata(L, idx ), l ); if(0) //屏蔽 meta 处理 
			if(lua_getmetatable(L, idx ) ) { 
				const int k= _cloneRecursion(L, lua_gettop(L ), L_, 
					((0<= r_lv )? r_lv: -1 ) ); lua_pop(L, 1 ); 
				if((0<= r_lv )&& lua_istable(L_, -1 )&& (0< k ) ) 
					printf("%*.s__{%3d}\n", (r_lv- 1 )* 2, "", k ); 
				lua_setmetatable(L_, -2 ); 
			} 
		} break; 
		case LUA_TFUNCTION: 
		if(lua_iscfunction(L, idx ) ) { 
			int j= 1; lua_CFunction f= lua_tocfunction(L, idx ); 
			for(j= 1; UPVALUE_MAX>= j; ++j ) { 
				//设置函数的 upvalue 
				if(!lua_getupvalue(L, idx, j ) ) break; 
				_cloneRecursion(L, lua_gettop(L ), L_, 
					((0<= r_lv )? (r_lv+ 1 ): -1 ) ); lua_pop(L, 1 ); 
			} lua_pushcclosure(L_, f, j- 1 ); 
		} else 
		{ int j= 1; DUMP_CB ud; memset(&ud, 0, sizeof(ud ) ); 
			lua_pushvalue(L, idx ); lua_dump(L, writer_CB, &ud ); 
			if(ud.p ) { //载入函数到新的栈 
				lua_load(L_, reader_CB, &ud, (char * )0 ); free(ud.p ); 
			} lua_pop(L, 1 ); 
			for(j= 1; UPVALUE_MAX>= j; ++j ) { 
				//设置函数的 upvalue 
				if(!lua_getupvalue(L, idx, j ) ) break; 
printf("upvalue %d\n", j ); 
				_cloneRecursion(L, lua_gettop(L ), L_, 
					((0<= r_lv )? (r_lv+ 1 ): -1 ) ); lua_pop(L, 1 ); 
				lua_setupvalue(L_, -2, j ); 
			} 
		} break; 
		case LUA_TNIL: 
		lua_pushnil(L_ ); 
		break; 
		case LUA_TNONE: 
		case LUA_TTHREAD: //?
		default: 
		ret_= 0; break; 
	} 
	return ret_; 
} 
int _cloneRecursion(lua_State *L, const int obj, 
	lua_State *L_, const int r_lv ) { 
	int tbl_, count= 0; 
	if(_cloneNoTable(L, obj, L_, r_lv ) ) 
		return 0; // 不是表, 直接返回 
	
	lua_rawgeti(L_S_, LUA_REGISTRYINDEX, (int )L_S_ ); 
	lua_pushvalue(L, obj ); lua_pushvalue(L, obj ); 
	lua_xmove(L, L_S_, 2 ); lua_rawget(L_S_, -3 ); 
	if(lua_isnil(L_S_, -1 ) ) lua_pop(L_S_, 1 ); else 
	{ lua_xmove(L_S_, L_, 1 ); // 返回 -1 表示已经存在 
		lua_pop(L_S_, 2 ); return -1; } 
	
	lua_newtable(L_ ); tbl_= lua_gettop(L_ ); // tbl_= {} 
	lua_pushvalue(L_, -1 ); 
	lua_xmove(L_, L_S_, 1 ); lua_rawset(L_S_, -3 ); 
	lua_pop(L_S_, 1 ); 
	
	lua_pushnil(L ); while(lua_next(L, obj ) ) { 
		int k= lua_gettop(L )- 1; ++count; 
		if(0<= r_lv ) { // 打印信息 
			if(lua_type(L, k )!= LUA_TSTRING ) 
				{ lua_pushvalue(L, k ); k= -1; } 
			printf("%*.s[%s %d]\n", r_lv* 2, "", 
				lua_tostring(L, k ), lua_type(L, k+ 1 ) ); 
			if(-1== k ) { lua_pop(L, 1 ); k= lua_gettop(L )- 1; } 
		} 
		_cloneRecursion(L, k, L_, 
			((0<= r_lv )? (r_lv+ 1 ): -1 ) ); // _copy(k ) 
		k= _cloneRecursion(L, k+ 1, L_, 
			((0<= r_lv )? (r_lv+ 1 ): -1 ) ); // _copy(v ) 
		if((0<= r_lv )&& lua_istable(L_, -1 ) ) // 打印信息 
			printf("%*.s__{%3d}\n", r_lv* 2, "", k ); 
		lua_rawset(L_, tbl_ ); // tbl_[_copy(k ) ]= _copy(v ) 
		lua_pop(L, 1 ); // 让 key 留在栈顶 
	} 
	if(lua_getmetatable(L, obj ) ) { 
		const int k= _cloneRecursion(L, lua_gettop(L ), L_, 
			((0<= r_lv )? r_lv: -1 ) ); lua_pop(L, 1 ); 
		if((0<= r_lv )&& lua_istable(L_, -1 )&& (0< k ) ) 
			printf("%*.s__{%3d}\n", (r_lv- 1 )* 2, "", k ); 
		lua_setmetatable(L_, tbl_ ); 
	} 
	return count; 
} 
inline void CLONE(lua_State *L, lua_State *L_, int idx ) { 
	static int REF_lookup= 0; 
	if(!REF_lookup ) { lua_newtable(L_S_ ); 
		lua_rawseti(L_S_, LUA_REGISTRYINDEX, (int )L_S_ ); } 
	++REF_lookup; if(0> idx ) idx= lua_gettop(L )+ 1+ idx; 
	_cloneRecursion(L, idx, L_, -1 ); 
	if(!--REF_lookup ) { lua_pushnil(L_S_ ); 
		lua_rawseti(L_S_, LUA_REGISTRYINDEX, (int )L_S_ ); } 
} 

//--------------------------------------------------------------
#if defined(_WIN32 ) 
__declspec(dllexport ) 
#else 
__attribute__ ((visibility("default" ) ) ) 
#endif // ! __ANDROID__ 
int luaopen_shareMT(lua_State *L ) { 
	int i= 0; 
	_INIT_LIB_LUA((char * )0 ); 
	_INIT_LIB_LUALIB((char * )0 ); 
	_INIT_LIB_LAUXLIB((char * )0 ); 
  
	if(!L_S_ ) { if(i ); 
		pthread_mutex_init(&MUTEX_S_, NULL ); 
		//pthread_rwlock_init(&RWLOCK_S_, NULL ); 
		L_S_= lua_open(); luaL_openlibs(L_S_ ); 
		lua_newtable(L_S_ ); 
		lua_rawseti(L_S_, LUA_REGISTRYINDEX, 
			(int )__SMT_CLONE_FUNC ); 
		memset(__th_state, 0, sizeof(__th_state ) ); 
	} 
	
	lua_newtable(L ); 
	lua_pushstring(L, "简单的 LUA 多线程支持\n" ); 
	lua_setfield(L, -2, "__readme" ); 
	lua_pushcclosure(L, sth_run, 0 ); 
	lua_setfield(L, -2, "run" ); 
	
	lua_pushcclosure(L, sth_dumy, 0 ); 
	lua_setfield(L, -2, "stop" ); 
	lua_pushcclosure(L, sth_dumy, 0 ); 
	lua_setfield(L, -2, "stat" ); 
	lua_pushcclosure(L, sth_dumy, 0 ); 
	lua_setfield(L, -2, "setdata" ); 
	
	lua_newtable(L ); 
	lua_pushcclosure(L, smt___index, 0 ); 
	lua_setfield(L, -2, "__index" ); 
	lua_pushcclosure(L, smt___newindex, 0 ); 
	lua_setfield(L, -2, "__newindex" ); 
	lua_setmetatable(L, -2 ); 
	
  return 1; 
} 

//--------------------------------------------------------------
//#define __TCCRUN 1 
#if defined(__TCCRUN ) 

static inline int TEST_01(lua_State *L ) { 
	long i= 0, j= 0; enum { TEST_COUNT= 100000, }; if(j); 
	
	lua_pushvalue(L, 1 ); 
	lua_pushstring(L, "_G" ); 
	i= luaL_dostring(L, "local t001= \"" 
			//"local l1=4 local function f1()for i=1,l1 do print(i)end end return f1" 
			"l1=100 local function f1() local t1,t2={},{} " 
			"for i=1,4 do t1['i'..i]=math.random(l1)end " 
			"for i=1,2 do t2[i]=math.random(50)end " 
			"t1.p=t2 t2.p=t1 return t1 end return f1()" 
			"\"" 
		); 
	//lua_pushcclosure(L, sth_dumy, 1 ); 
	//lua_pushvalue(L, -1 ); lua_setglobal(L, "os__" ); i= luaL_dostring(L, "print(os__(), '@@')" ); 
	i= GetTickCount(); for(j= 0; TEST_COUNT> j; ++j ) { 
		//printf("  !!top%3d %d\n", j, lua_gettop(L ) ); 
		lua_pushvalue(L, 1 ); lua_pushstring(L, "_G" ); 
		smt___index(L ); lua_pop(L, 3 ); 
		//printf("  !!top    %d\n", lua_gettop(L ) ); 
	} printf("%s tick[%d] %ld\n", __FUNCTION__, TEST_COUNT, GetTickCount()- i ); 
	i= smt___index(L ); 
	//CLONE(L, L_S_, lua_gettop(L ) ); 
	
printf("\nTEST top %d %d\n", lua_gettop(L ), lua_gettop(L_S_ ) ); 
printf("TEST %d %d\n\n", lua_type(L, -1 ), lua_type(L_S_, -1 ) ); 
	lua_setglobal(L, "os__" ); 
	i= luaL_dostring(L, 
			"for k,v in pairs(os__)do if 'table'==type(v) then print(k,v)end end" 
			//"for k,v in pairs(os__)do print(k,v)end" 
		); 
if(i ) printf("  err{%s}\n", lua_tostring(L_S_, -1 ) ); 
	return 0; 
} 
static inline int TEST_02(lua_State *L ) { 
	//printf("%*.s\n",8,"ABCDEFGHIJKLMN0123456789" ); 
	luaL_dostring(L, "return function() print('TTT' ) end" ); 
	int i= lua_pcall(L, 0, 1, 0 ); if(i);
} 

int main(int argc, char **argv ) { 
	_INIT_LIB_LUA((char * )0 ); 
	_INIT_LIB_LUALIB((char * )0 ); 
	_INIT_LIB_LAUXLIB((char * )0 ); 
	
	lua_State *L= lua_open(); int i= 0; luaL_openlibs(L ); 
	i= luaL_dostring(L, "io.stdout:setvbuf'no'" ); if(i ); 
	luaopen_shareMT(L ); 
	TEST_01(L ); 
	
	lua_close(L_S_ ); lua_close(L ); 
	return 0; 
} 

#endif // __TCCRUN 
 
