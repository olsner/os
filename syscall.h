namespace syscall {

extern "C" void syscall();

void syscall() {
	unimpl("syscall");
}

}
