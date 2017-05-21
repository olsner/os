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
