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
    AddressSpace *owner;
    Handle *other;
    u64 pulses;
    u8 type;

    Handle(uintptr_t key, AddressSpace *owner):
        node(key), owner(owner), other(nullptr), pulses(0) {}

    uintptr_t key() const { return node.key; }

    void dissociate() {
        if (other) {
            other->other = NULL;
            other = NULL;
        }
    }

    void associate(AddressSpace *p, Handle *g) {
        g->owner = p;
        g->other = this;
        other = g;
    }

    static void associate(AddressSpace *p, AddressSpace *q, Handle *h, Handle *g) {
        h->owner = q;
        h->other = g;

        g->owner = p;
        g->other = h;
    }
};

struct PendingPulse
{
    typedef uintptr_t Key;
    DictNode<Key, PendingPulse> node;

    PendingPulse(Handle *handle): node(handle->key()) {}
};

}
