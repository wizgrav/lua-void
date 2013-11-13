package = "Void"
version = "1.0-1"
source = {
   url= "git://github.com/wizgrav/lua-void.git",
	branch= "master"
}
description = {
   summary = "A typed array system for Lua that supports cross thread transfer of buffers.",
   detailed = [[
   Vision is module that implements an buffer system.
   The module features:
   * Buffers can be of arbitrary size and accessed as different types.
   * Views on the buffers can be created to operate on slices of them.
   * Named queues are provided for transfering buffers between threads.
   * Common operations like writing to/reading from files and sockers. 
   ]],
   homepage = "http://www.wizgrav.com",
   license = "MIT/X11",
   maintainer="Yannis Gravezas <wizgrav@gmail.com>"
}
dependencies = {
   "lua >= 5.1"
}
build = {
	type = "builtin",
	modules = {
		void = {
			 sources = {"src/void.c"},
			 libraries = {"pthread"}
		}
	}
}
