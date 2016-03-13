; vim:filetype=nasm:

; There is a single syscall that does IPC.
; Fixed parameters:
; * rax: message number and flags (flags specify the direction and kind of ipc)
; * rdi: target handle
; Message registers: rsi, rdx, r8, r9, r10
; syscall-clobbered: rcx = rip, r11 = flags
; syscall-preserved: rbp, rbx, r12-r15, rsp
; Return value(s):
; * rax: message/error code for receives, error code for sends
; * rdi: source handle of return value, 0, or the target handle we failed to
; send to
; * message registers for receives

; SYSCALLS!
; TODO Replace non-IPC syscalls with IPC (perhaps to specific objects), to free
; registers from mostly-uninteresting syscall number to specify message numbe
; instead.
; special object values should be avoided - using 0/-1 for non/all might allow
; send, sendrcv, rcv to be merged. (though some more flags might be needed?)
;
; arguments: rdi, rsi, rdx, (not rcx! rcx stores rip), r8, r9, r10 (used instead
; of rcx in syscalls)
; syscall-clobbered: rcx = rip, r11 = flags
; syscall-preserved: rbp, rbx, r12-r15, rsp
; return value(s): rax, rdx
;
; Message-passing through registers uses rdi and rsi to send the message code
; and sender/recipient, rax to receive the message code. rdx, r8-10 are message
; parameters.

SYSCALL_WRITE	equ	0
SYSCALL_GETTIME	equ	1

; Create a new process. Is this similar to fork? Details not really determined.
;
; rdi: entry-point for new process
; rsi: end of new process' load segment
;
; The stack for the new process will start just before the page that has the
; entry point.
;
; Returns:
; rax: process handle for child process or 0 on error (but what about error
; codes?)
; The entry point will have
; rdi: process handle for parent process
; rsi: ???
; rax: 0
; (remaining registers could be parameters for the process entry point, copied
; from the calling process)
SYSCALL_NEWPROC	equ	3

; Send a message, wait synchronously for response from the same process.
; Works the same as SYSCALL_SEND followed by SYSCALL_RECV but more efficient.
;
; Should add some way to let the receive part be a receive-from-any. A server
; can get rid of a lot of syscalls by allowing it to send the response to proc
; A at the same time as it receives the next request from any process.
; Otherwise it'd need to SEND then return back just so that it can do a new
; receive-from-any RECV.
;
; Takes:
; rdi: message code
; rsi: send-to (and receive-from) process handle.
; <message parameters>
;
; (See also SYSCALL_SEND)
;
; Returns:
; If the send was successful: see SYSCALL_RECV.
; Otherwise, you'll get an error response in rax, and you'll probably not know
; if it was the SEND or the RECV that failed. (TBD: would such information be
; reliably useful?)
SYSCALL_SENDRCV	equ	4

; Receive a message
;
; rdi: receive-from (0 = receive-from-any)
;
; Returns in registers:
; rax: message code
; rdi: sending process ID
; Remaining argument registers: message data
SYSCALL_RECV	equ	5

; Send a message
; rdi: message code
; rsi: target process ID
; Remaining argument registers: message data
; Returns:
; rax: error code or 0
SYSCALL_SEND	equ	6

SYSCALL_HALT	equ	7
