namespace {

enum HandleType {
    USER_HANDLE = 0,
    ASPACE_HANDLE = 1,
    THREAD_HANDLE = 2,
};

struct Handle
{
    typedef uintptr_t Key;
    DictNode<Key, Handle> node;
    // Weak/non-owning pointers so they shouldn't be shared_ptr's.
    // Should possibly be weak_ptr to make it really safe, though that will
    // require additional support in shared_ptr, meanwhile handles are supposed
    // to be dissociated as appropriate, which otoh means thread-unsafe
    // cross-cpu access to handle objects when processes on different cpus
    // delete/modify their handles...
    AddressSpace *otherspace;
    Handle *other;
    u64 events;
    u8 type;

    // Assume 0-init!
    Handle(uintptr_t key, AddressSpace *otherspace):
        node(key), otherspace(otherspace) {}

    uintptr_t key() const { return node.key; }

    void dissociate() {
        if (other) {
            other->other = NULL;
            other = NULL;
        }
    }

    void associate(const RefCnt<AddressSpace> &p, Handle *g) {
        associate(p.get(), g);
    }
    void associate(AddressSpace *p, Handle *g) {
        g->otherspace = p;
        g->other = this;
        other = g;
    }

    static void associate(AddressSpace *p, AddressSpace *q, Handle *h, Handle *g) {
        h->otherspace = q;
        h->other = g;

        g->otherspace = p;
        g->other = h;
    }
};

struct PendingPulse
{
    typedef uintptr_t Key;
    DictNode<Key, PendingPulse> node;

    PendingPulse(Handle *handle): node(handle->key()) {}

    uintptr_t key() const { return node.key; }
};
}
