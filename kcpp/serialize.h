namespace {
void write_u8(void *p, u8 data) {
	*(u8 *)p = data;
}
void write_u16(void *p, u16 data) {
	*(u16 *)p = data;
}
void write_u32(void *p, u32 data) {
	*(u32 *)p = data;
}
void write_u64(void *p, u64 data) {
	*(u64 *)p = data;
}
}
