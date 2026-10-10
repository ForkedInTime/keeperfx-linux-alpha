// Level/campaign Lua may read files but not change them (src/lua_readonly_io.c).
//
// The state is built the way open_lua_script() builds it: the same standard
// libraries, upstream's sandbox (io keeps only open, os only remove,
// package.loadlib gone), then the real lua_make_files_read_only(). Then a
// script does what a hostile map would, and what the game's own scripts need.
#include <lua.hpp>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/lua_readonly_io.h"

// What the linked engine objects expect from the rest of the game.
extern "C" void LbErrorParachuteInstall() {}
extern "C" int kfxmain(int, char **) { return 0; }
class WindowSystemSDL;
WindowSystemSDL* GetSDLWindowSystem() { return nullptr; }
extern "C" { char keeper_runtime_directory[152] = ""; }
#include "../src/bflib_datetm.h"
extern "C" TbResult LbDateTime(struct TbDate * date, struct TbTime * tm)
{
	std::memset(date, 0, sizeof(*date));
	std::memset(tm, 0, sizeof(*tm));
	return Lb_SUCCESS;
}
static int g_warnings = 0;
extern "C" int LbWarnLog(const char *format, ...)
{
	g_warnings++;
	va_list ap;
	va_start(ap, format);
	std::fprintf(stderr, "    (warning) ");
	std::vfprintf(stderr, format, ap);
	va_end(ap);
	return 0;
}
extern "C" unsigned int get_gameturn() { return 0; }

static int g_failures = 0;

static void expect(bool ok, const char * what)
{
	std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++g_failures;
}

static bool file_exists(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0; }

static std::string read_file(const std::string &p)
{
	std::string out;
	FILE *f = std::fopen(p.c_str(), "rb");
	if (!f) return out;
	char buf[256]; size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
	std::fclose(f);
	return out;
}

static void write_file(const std::string &p, const char *s)
{
	FILE *f = std::fopen(p.c_str(), "wb");
	std::fputs(s, f);
	std::fclose(f);
}

// open_lua_script() and disable_lua_functions(), as upstream has them (#5389).
static lua_State *engine_like_state()
{
	lua_State *L = luaL_newstate();
	static const luaL_Reg stdlib[] = {
		{"",               luaopen_base},
		{LUA_TABLIBNAME,   luaopen_table},
		{LUA_IOLIBNAME,    luaopen_io},
		{LUA_OSLIBNAME,    luaopen_os},
		{LUA_STRLIBNAME,   luaopen_string},
		{LUA_MATHLIBNAME,  luaopen_math},
		{LUA_DBLIBNAME,    luaopen_debug},
		{LUA_LOADLIBNAME,  luaopen_package},
		{LUA_BITLIBNAME,   luaopen_bit},
		{NULL, NULL}
	};
	for (const luaL_Reg *lib = stdlib; lib->func != NULL; lib++) {
		lua_pushcfunction(L, lib->func);
		lua_pushstring(L, lib->name);
		lua_call(L, 1, 0);
	}
	const char *const keep[][2] = {{"io", "open"}, {"os", "remove"}};
	for (auto &k : keep) {
		lua_getglobal(L, k[0]);
		int t = lua_gettop(L);
		lua_pushnil(L);
		while (lua_next(L, t) != 0) {
			if (lua_type(L, -2) == LUA_TSTRING && std::strcmp(lua_tostring(L, -2), k[1]) != 0) {
				lua_pushvalue(L, -2);
				lua_pushnil(L);
				lua_rawset(L, t);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
	lua_getglobal(L, "package");
	lua_pushnil(L);
	lua_setfield(L, -2, "loadlib");
	lua_pop(L, 1);
	return L;
}

// Runs chunk; it must return true. The message names what it checks.
static void lua_expect(lua_State *L, const char *chunk, const char *what)
{
	// Restores the stack to where it was, not to empty: main() keeps the
	// original functions on it.
	const int base = lua_gettop(L);
	bool ok = false;
	if (luaL_loadstring(L, chunk) != 0) {
		std::printf("    (compile error: %s)\n", lua_tostring(L, -1));
	} else if (lua_pcall(L, 0, 1, 0) != 0) {
		std::printf("    (error: %s)\n", lua_tostring(L, -1));
	} else {
		ok = lua_toboolean(L, -1);
	}
	lua_settop(L, base);
	expect(ok, what);
}

static void set_global(lua_State *L, const char *name, const std::string &value)
{
	lua_pushstring(L, value.c_str());
	lua_setglobal(L, name);
}

int main(int argc, char **argv)
{
	const char *root = (argc > 1) ? argv[1] : ".";
	char tmpl[] = "/tmp/kfx-lua-ro-XXXXXX";
	std::string dir = mkdtemp(tmpl);
	write_file(dir + "/data.txt", "first line\nsecond line\n42 7\ntail");
	write_file(dir + "/mixed.txt", "case\n");
	write_file(dir + "/keep.txt", "untouched");

	// A native module a map pack could ship beside its scripts: loading it
	// would create the marker file.
	write_file(dir + "/evil.c",
		"#include <stdio.h>\n"
		"int luaopen_evil(void *L) { FILE *f = fopen(\"" "MARKER" "\", \"w\"); if (f) fclose(f); return 0; }\n");
	{
		std::string src = read_file(dir + "/evil.c");
		std::string marker = dir + "/native-code-ran";
		src.replace(src.find("MARKER"), 6, marker);
		write_file(dir + "/evil.c", src.c_str());
		std::string cmd = "cc -shared -fPIC -o " + dir + "/evil.so " + dir + "/evil.c";
		if (std::system(cmd.c_str()) != 0) std::printf("    (could not build the test module)\n");
	}

	std::printf("Lua scripts: read-only file access\n");
	lua_State *L = engine_like_state();

	// Kept on the C stack, out of any script's reach, to check below that
	// nothing a script can reach still holds them.
	lua_getglobal(L, "io");   lua_getfield(L, -1, "open");   lua_remove(L, -2);
	lua_getglobal(L, "os");   lua_getfield(L, -1, "remove"); lua_remove(L, -2);
	const int orig_open = lua_gettop(L) - 1, orig_remove = lua_gettop(L);

	lua_make_files_read_only(L);
	set_global(L, "D", dir);

	std::printf("writing is refused, the way a failed open always reported it\n");
	lua_expect(L,
		"for _, m in ipairs({'w','a','r+','w+','a+','wb','rw','rb+',''}) do\n"
		"  local f, msg, code = io.open(D..'/new.txt', m)\n"
		"  if f ~= nil or type(msg) ~= 'string' or code == nil then return false end\n"
		"  f, msg = io.open(D..'/keep.txt', m)\n"
		"  if f ~= nil then return false end\n"
		"end\n"
		"return true", "io.open with every write/append/update mode returns nil, message, code");
	expect(!file_exists(dir + "/new.txt"), "no file was created");
	expect(read_file(dir + "/keep.txt") == "untouched", "an existing file was not truncated");
	lua_expect(L,
		"local ok, msg = os.remove(D..'/keep.txt')\n"
		"return ok == nil and msg:find('read files') ~= nil", "os.remove returns nil and a message");
	expect(file_exists(dir + "/keep.txt"), "the file is still there");

	std::printf("reading works as Lua's own io does\n");
	lua_expect(L,
		"local f = assert(io.open(D..'/data.txt'))\n"
		"local a = f:read('*l'); local b = f:read('l')\n"
		"local n1, n2 = f:read('*n', 'n')\n"
		"local rest = f:read('*a')\n"
		"local eof = f:read(0)\n"
		"f:close()\n"
		"return a == 'first line' and b == 'second line' and n1 == 42 and n2 == 7\n"
		"   and rest == '\\ntail' and eof == nil", "read: lines, numbers, all, and end-of-file");
	lua_expect(L,
		"local f = assert(io.open(D..'/data.txt', 'rb'))\n"
		"local five = f:read(5)\n"
		"local pos = f:seek('set', 6); local word = f:read(4)\n"
		"local size = f:seek('end')\n"
		"local withnl = (f:seek('set', 0) and f:read('*L'))\n"
		"f:close()\n"
		"return five == 'first' and pos == 6 and word == 'line' and size == 32 and withnl == 'first line\\n'",
		"read counts, seek and keep-newline lines");
	lua_expect(L,
		"local f = assert(io.open(D..'/data.txt'))\n"
		"local n = 0; for l in f:lines() do n = n + 1 end\n"
		"f:close()\n"
		"return n == 4", "f:lines()");
	lua_expect(L,
		"local f = assert(io.open(D..'/MIXED.TXT'))\n"
		"local s = f:read('*l'); f:close(); return s == 'case'",
		"a name in the wrong case opens the file, as the game's other Lua loading does");
	lua_expect(L,
		"local f = assert(io.open(D..'/data.txt')); f:close()\n"
		"local ok = pcall(f.read, f, '*l')\n"
		"return not ok and tostring(f) == 'file (closed)'", "a closed file cannot be read");
	lua_expect(L,
		"local f, msg = io.open(D..'/does-not-exist')\n"
		"return f == nil and msg:find('No such file') ~= nil", "a missing file reports why");
	lua_expect(L,
		"local f = assert(io.open(D..'/data.txt'))\n"
		"return f.write == nil and f.flush == nil and f.setvbuf == nil", "a handle has no writing methods");

	std::printf("handles cannot be forged or exhausted\n");
	lua_expect(L,
		"local mt = debug.getregistry()['KeeperFX.ReadOnlyFile']\n"
		"local fake = newproxy(true)\n"
		"debug.setmetatable(fake, mt)\n"
		"local ok, msg = pcall(mt.__index.read, fake, '*a')\n"
		"return not ok and tostring(msg):find('not a file') ~= nil",
		"a userdata given the file metatable is rejected, not read as a handle");
	lua_expect(L,
		"collectgarbage(); collectgarbage()\n"
		"local held = {}\n"
		"for i = 1, 40 do held[i] = io.open(D..'/data.txt') end\n"
		"local opened = 0; for i = 1, 40 do if held[i] then opened = opened + 1 end end\n"
		"held = nil; collectgarbage(); collectgarbage()\n"
		"local again = io.open(D..'/data.txt')\n"
		"return opened == 32 and again ~= nil", "at most 32 open at once, and dropped handles are closed by the collector");

	std::printf("no native code through require\n");
	lua_expect(L,
		"package.cpath = D..'/?.so'\n"
		"local ok = pcall(require, 'evil')\n"
		"return not ok", "require cannot load a .so, even with package.cpath set by the script");
	expect(!file_exists(dir + "/native-code-ran"), "the module's code never ran");
	lua_expect(L, "return not pcall(require, 'ffi') and not pcall(require, 'jit')", "neither ffi nor jit can be required");
	// setLuaPath() appends this edition's case-insensitive searcher at
	// #package.loaders + 1 after this runs; it must land right after the Lua
	// searcher, with no hole for require() to stop at.
	lua_expect(L,
		"if #package.loaders ~= 2 then return false end\n"
		"package.loaders[#package.loaders + 1] = function(name)\n"
		"  if name == 'probe' then return function() return 'found' end end\n"
		"end\n"
		"return require('probe') == 'found' and #package.loaders == 3",
		"a searcher appended afterwards, as setLuaPath() does, is consulted");

	std::printf("the originals are unreachable\n");
	// Walk everything a script can reach: globals, the registry, metatables,
	// function environments and upvalues (Lua and C), recording each function.
	const char *walker =
		"local seen, funcs = {}, {}\n"
		"local function walk(v)\n"
		"  local t = type(v)\n"
		"  if (t ~= 'table' and t ~= 'function' and t ~= 'userdata' and t ~= 'thread') or seen[v] then return end\n"
		"  seen[v] = true\n"
		"  if t == 'function' then\n"
		"    funcs[v] = true\n"
		"    walk(debug.getfenv(v))\n"
		"    local i = 1\n"
		"    while true do local n, uv = debug.getupvalue(v, i); if n == nil then break end; walk(uv); i = i + 1 end\n"
		"  elseif t == 'table' then\n"
		"    for k, x in next, v do walk(k); walk(x) end\n"
		"  end\n"
		"  walk(debug.getmetatable(v))\n"
		"end\n"
		"walk(_G); walk(debug.getregistry()); walk(package.loaded)\n"
		"return funcs";
	if (lua_gettop(L) != orig_remove) {
		std::printf("    (the saved originals were lost from the stack)\n");
		expect(false, "reachability walk");
	} else if (luaL_loadstring(L, walker) == 0 && lua_pcall(L, 0, 1, 0) == 0) {
		lua_pushvalue(L, orig_open);   lua_rawget(L, -2); bool open_found = lua_toboolean(L, -1); lua_pop(L, 1);
		lua_pushvalue(L, orig_remove); lua_rawget(L, -2); bool remove_found = lua_toboolean(L, -1); lua_pop(L, 1);
		expect(!open_found, "the original io.open is not reachable from any script");
		expect(!remove_found, "the original os.remove is not reachable from any script");
		lua_pop(L, 1);
	} else {
		std::printf("    (walker failed: %s)\n", lua_tostring(L, -1));
		expect(false, "reachability walk");
	}

	std::printf("what the game's own scripts need still works\n");
	set_global(L, "ROOT", root);
	lua_expect(L,
		"local f = string.dump(function() return 42 end)\n"
		"return load(f, nil, 'b', _G)() == 42",
		"loading bytecode, which core/serialisation.lua uses to restore saved functions");
	lua_expect(L,
		"package.path = ROOT..'/config/fxdata/lua/?.lua;'..package.path\n"
		"local binser = require 'external.binser'\n"
		"local inspect = require 'external.inspect'\n"
		"local s = binser.serialize({a = 1, b = {'x', 'y'}})\n"
		"local t = binser.deserialize(s)\n"
		"return t[1].a == 1 and t[1].b[2] == 'y' and inspect({1}) == '{ 1 }'",
		"the bundled serialiser and inspector load and round-trip");
	lua_expect(L,
		"return type(debug.getregistry()) == 'table' and debug.getinfo(print, 'S').what == 'C'\n"
		"   and debug.getupvalue((function() local x = 1; return function() return x end end)(), 1) == 'x'",
		"the debug functions serialisation and triggers use");

	lua_close(L);
	expect(g_warnings > 0, "refusals were logged");

	std::string cleanup = "rm -rf " + dir;
	if (std::system(cleanup.c_str()) != 0) std::printf("    (could not remove %s)\n", dir.c_str());

	if (g_failures) {
		std::printf("%d Lua read-only check(s) FAILED\n", g_failures);
		return 1;
	}
	std::printf("All Lua read-only checks passed.\n");
	return 0;
}
