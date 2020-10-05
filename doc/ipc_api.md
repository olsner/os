# IPC API (v1) #

This version tried to be as simple as possible, but I think it ended up a bit
too simple.

The basics was a handle (arbitrary 64-bit ID, e.g. pointer-like), besides
preallocated handles handles were created by duplicating an existing handle or
doing a "fresh" receive.

Receiving from any unassigned handle ID was an open-ended receive that also
assigns the incoming handle to itself if it isn't already allocated. There was
no way to find what original handle that came from, so making derived handles
was tricky.

Each handle was tied to exactly one process, which wouldn't support
multi-threading in a good way.


# IPC API (v2) #

Some goals of the new API are to:

* Allow the kernel to allocate a handle when necessary, e.g. to implement
  transfer of handles.

* Support multi-threading (at least in theory)
  - multiple threads doing concurrent requests to the same file/object
  - servers with multiple worker threads

  (Provided by the transaction API.)

* Provide a reasonable API to create derived fds from existing handles, e.g.
  `openat` or similar APIs. Make sure the server can record any state it needs
  to keep track of the new handle.

  (Provided by transactions with the `MSG_TX_ACCEPTFD` flag.)

* Provide something that could look like sockets or pipes


The new API provides file descriptors that work like in POSIX. They're
allocated in increasing order (lowest free number first), and are closed by
`close()`. Multiple file descriptors may refer to the same underlying file.

`dup()` (previously `hmod`) is no longer implemented as it was not necessary
for the current set of programs with other IPC changes.


# Transactions #

Each socket allows a small number of concurrent pending transaction, these
allow the response to a call to be routed back to the correct thread/process
even for shared file descriptors or concurrent requests.

The transaction is started by the client doing a `call` IPC to a server.

The server's receive call gets the transaction ID along with flags and the file
descriptor, and the transaction is finished by the server doing a `send` back
to the client with the matching transaction id in the destination argument.

Recording the received txid+fd and passing it directly to send will route the
reply to the correct thread and file. The transaction ID is assigned by the
kernel and should not be set in the original `call` IPC.

Transactions may use the `MSG_TX_ACCEPTFD` flag when starting the transaction,
which signals that the caller is prepared for one of the returned values to be
a file descriptor.

On the reply side, `MSG_TX_FD` (same bit value as `MSG_TX_ACCEPTFD`) signals
that a file descriptor is included as the first message register. If
`MSG_TX_CLOSEFD` is also set, the local file descriptor is closed after
duplicating it in the recipient.

# System calls #

## socketpair ##

Creates two connected anonymous sockets. The two ends are symmetrical. This is
the main way of creating a new file descriptor before e.g. sending it to a
client in an IPC reply.

Takes no arguments.

Returns a negative error in `rax` or two new file descriptors >= 0 in `rax` and
`rdx`. It's undefined which order the two file descriptors are allocated or
returned.

## close ##

Takes one argument, the file descriptor to close.

Returns a negative error or 0 in `rax`.

## recv ##

Takes one argument containing the file descriptor to receive from and flags.
No flags are currently defined for receive, but it could include things like
acceptfd (usually only allowed on calls) or non-blocking receive.

If the file descriptor is -1, accept messages from any open file descriptor in
the process. Given that multiple file descriptors may be connected to the same
file, it is indeterminate which file descriptor will be returned for a socket
with pending IPC.

In the future, it's expected that some action is necessary to enable open-ended
receive to consider a file descriptor, or making a polling object similar to
epoll file descriptors where things can be registered and waited for.


If the received message came from a call (as opposed to a send), the message
flags will include `MSG_KIND_CALL`, and the reply is guaranteed not to block.
For calls, the transaction flags and ID might also be used. txid includes
transaction flags such as `MSG_TX_ACCEPTFD` and a transaction ID if the message
was a call.


Input:

* `rax`: 0 (syscall/message number)
* `rdi`: fd | flags

Output (error):

* `rax`: negative errno

Output (success):

* `rax`: flags | msg
* `rdi`: txid | sender-fd
* `{ rsi, rdx, r8, r9, r10 }`: message arguments 1..5

Errors:

* `EBADF`: invalid file descriptor (except for -1 which has special meaning)

## send ##

Send an IPC message to the other end of a socket or reply to an incoming IPC
transaction.

When responding to a transaction with `MSG_TX_ACCEPTFD` set in txflags, the
server must set `MSG_TX_FD` and may set `MSG_TX_CLOSEFD` when sending its
reply. The first message argument will then be expected to be a file descriptor
which will be copied into the recipient process. If `MSG_TX_CLOSEFD` is set,
the file descriptor on the sender side will be closed at the same time.

There is or will be a way to send an error without a file descriptor.

Input:

* `rax`: msg (`MSG_USER` .. `MSG_USER_LAST`, i.e. 16..255)
* `rdi`: txflags | fd
* `rsi`: message argument 1, or file descriptor (if `txflags & MSG_TX_FD`)
* `{ rdx, r8, r9, r10 }`: message arguments 2..5

Output:

* `rax`: 0 on success or a negative errno

## call ##

Start a new transaction and wait for a reply. The server finishes the
transaction by replying with 'send'.

If the client's txflags includes `MSG_TX_ACCEPTFD`, the reply (if not an error)
must include a file descriptor.

Input:

* `rax`: `MSG_KIND_CALL` | msg
* `rdi`: txflags | fd
* `{ rsi, rdx, r8, r9, r10 }`: message arguments 1..5

Output:

* `rax`: msg or negative errno
* `rdi`: txflags | fd
* `rsi`: message argument 1, or file descriptor (if `txflags & MSG_TX_FD`)
* `{ rdx, r8, r9, r10 }`: message arguments 2..5
