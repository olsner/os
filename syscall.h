namespace syscall {

extern "C" void syscall(u64, u64, u64, u64, u64, u64, u64) NORETURN;

void syscall(u64 arg0, u64 arg1, u64 arg2, u64 arg5, u64 arg3, u64 arg4, u64 nr) {
    printf("syscall %#x: %lx %lx %lx %lx %lx %lx\n", (unsigned)nr, arg0, arg1, arg2, arg3, arg4, arg5);
	unimpl("syscall");
}

}
