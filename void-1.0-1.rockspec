package = "Void"
version = "1.0-1"
source = {
   url= "git://github.com/wizgrav/lua-void.git",
	branch= "master"
}
description = {
   summary = "Memory buffers that support typed accesing and transfer between threads.",
   detailed = [[
   Void is module for memory buffer allocation and manipulation.
   The module features:
   * Buffers sizes up to 2GB and accessors for signed or unsigned types.
   * Proxy userdata as views pointing to slices of the allocated memory.
   * Named queues which allow for transfering buffers between threads.
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
	platforms = {
		unix = {
			modules = {
				void = {
					 libraries = {"pthreads"},
				}
			}
		},
		windows = {
			modules = {
				void = {
					 libraries = {"Ws2_32"},
				}
			}
		}
	},
	modules = {
		void = {
			 sources = {"src/void.c"},
		}
	}
}
