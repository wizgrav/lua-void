###void = require "void"###

A module that facilitates typed accesing of buffers and transferring them
between threads (works with all threading systems). The void.view userdata point to slices of their underlying buffers. The latter can be separated from the views by transferring them to other threads. In this case a view is considered neutered and must be either realloced(see void()) or reattached with a buffer via a void.link.

Main module functions
---------

###void.view = void(size, _view_) (call operator)###

Allocates a new void.view or reallocates an existing one, if provided, with size bytes

Returns a void.view representing the full slice of the underlying buffer


###void.link = void\[string\] (index operator)###

Returns a reference to a shared queue for interthread buffer transfer

The same identifier should be used in all threads for accessing a particular shared queue. They are automatically created and gced as needed

Each queue has it own lock for buffer operations but a global lock is used when indexing the void module, so the best practice is to do a _local link = void.mylink_ and operate on the local reference than constantly calling _void.mylink_


***
void.view
---------
You can attach your own metaevents(like gc) on the view metatable except index, newindex, call and len. In addition all other properties you attach to the metatable will be available to all void.views(read and write below are default methods)

###view.type = string ###

gets/sets the type of the view, affects the len operator(#)

can be u(nsigned) or s(igned) 8,16,32 bit ints, floats and doubles

arguments are "u8","s8","u16","s16","u32","s32","float","double"

###view.from = number ###

gets/sets the start of the view relative to the underlying buffer

along with size it defines a slice of the underlying buffer

counts from 1 and is always in bytes regardless of type

###view.size = number ###

gets/sets the size in bytes of the viewed slice

###view.blob###

gets the total size in bytes of the underlying buffer

### #view (len operator)###

gets the number of elements of the view based on type,from and size

###view\[index\] = number (index operator)###

Get/set the specified indexes based on type

###view(index, count=1) (call operator)###

Mass getter. Returns _count_ values starting from index

When called with no arguments returns the view slice as a string

###view\[0\] = void.view | string | number###

Gets a lightuserdata pointing at the start of the slice or

Sets the contents of the view from the provided string/view slice or

sets all elements of the slice to the provided number(casted to type)

### bytes|nil = view:read(filehandle | number fd)###

reads view.size bytes from the filehandle or socket fd(socket:getfd())

returns the amount read or nil if some error occured

### bytes|nil = view:write(filehandle | number fd)###

reads view.size bytes from the filehandle or socket fd(socket:getfd())

returns the amount read or nil if some error occured

### view_index, buffer_index = view:find(substring)###

Locates the index of the provided substring ***in the current view slice***

returns the indexes, relative to the view slice and the full buffer, where the substring starts or nils if it wasn't found.

***
void.link
---------
You can attach a gc metamethod on the link metatable which triggers when you lose the local reference, but the shared queue will be automatically disposed when there are no references to it and doen't have any buffers attached. In addition all other properties you attach to the metatable will be available to all void.links ***in the same thread you set them***

### #link (len operator)###

gets the count of buffers currently attached to the share queue.

###void.view | nil = link\[index\] (index operator - getter)###

It pops a buffer from the queue and returns a new view with the buffer attached to it

If the queue is empty or its buffer count is less than index it returns nil

A negative index will block the thread until the operation, indicated by the absolute value of the index, can be performed

###link\[index\] = string | void.view | nil (index operator - setter)###

It pushes the buffer provided to the queue(or a copy of the string or doesn't push if nil was provided)

If the queue length is >= than index it also pops a buffer and attaches it to the void.view(or frees it if string/nil was provided)

A negative index will block the thread until the operation, indicated by the absolute value of the index, can be performed

The provided void.view can come out bufferless(neutered) in this case a buffer must be reattached to access it again

###link(view,wait) ###

It swaps the buffer at the top of the queue with the one provided, if the queue is empty or __the last push/swap happened on a different thread__. This is useful to implement triple buffering setups

If wait is provided and true it will block until another thread pushes/swaps a buffer
