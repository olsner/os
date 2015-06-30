template <class K, class V> struct DictNode;

template <class K, class V> struct DictNode
{
    K key;
    DictNode* left;
    DictNode* right;

    DictNode(K key): key(key), left(nullptr), right(nullptr) {}

    static DictNode *Node(V *item) {
        return NodeFromItem(item);
    }
    V* Item() {
        return add_byte_offset(this, (uintptr_t)Node((V*)1));
    }
};
template <class V, class K = typename V::Key> struct Dict
{
    typedef DictNode<K, V> Node;
    Node* root;

    Dict(): root(nullptr) {}

    V* find(K key) {
        return NULL;
    }

    void insert(V* item) {
        Node *node = Node::Node(item);
        node->right = root;
        root = node;
    }
};

#define DICT_NODE(Class, member) \
    DictNode<Class::Key, Class> *NodeFromItem(Class *c) { return &c->member; }

namespace aspace {
enum MapFlags {
    MAP_X = 1 << 0,
    MAP_W = 1 << 1,
    MAP_R = 1 << 2,
    MAP_RX = MAP_R | MAP_X,
    MAP_RW = MAP_R | MAP_W,
    MAP_ANON = 1 << 3,
    MAP_PHYS = 1 << 4,
    MAP_DMA = MAP_ANON | MAP_PHYS,
    MAP_USER = MAP_DMA | MAP_RW | MAP_X,
};
struct MapCard {
    typedef uintptr_t Key;
    DictNode<Key, MapCard> as_node;
    uintptr_t handle;
    // .vaddr + .offset = handle-offset to be sent to backer on fault
    // For a direct physical mapping, paddr = .vaddr + .offset
    // .offset = handle-offset - vaddr
    // .offset = paddr - .vaddr
    // The low 12 bits contain flags.
    uintptr_t offset;

    MapCard(uintptr_t vaddr, uintptr_t handle, uintptr_t offset):
        as_node(vaddr),
        handle(handle),
        offset(offset)
    {}

    uintptr_t vaddr() const {
        return as_node.key;
    }
    uintptr_t paddr(uintptr_t vaddr) const {
        return vaddr + (offset & ~0xfff);
    }
    u16 flags() const {
        return offset & 0xfff;
    }
    void set(uintptr_t handle, uintptr_t offset) {
        this->handle = handle;
        this->offset = offset;
    }
};
DICT_NODE(MapCard, as_node);

typedef u64 PageTable[512];
typedef PageTable PML4;

PML4 *allocate_pml4() {
    PML4 *ret = (PML4 *)new PML4;
    (*ret)[511] = start32::kernel_pdp_addr | 3;
    return ret;
}

uintptr_t ToPhysAddr(const volatile void *p) {
    return (uintptr_t)p - kernel_base;
}

struct AddressSpace {
    PML4 *pml4;
    u32 count;

    AddressSpace(): pml4(allocate_pml4()), count(1) {}

    Dict<MapCard> mapcards;

    void mapcard_set(uintptr_t vaddr, uintptr_t handle, intptr_t offset, int flags) {
        if (MapCard *p = mapcards.find(vaddr)) {
            p->set(handle, offset | flags);
        } else {
            mapcards.insert(new MapCard(vaddr, handle, offset | flags));
        }
    }

    u64 cr3() const {
        return ToPhysAddr(pml4);
    }
};
}
