template <class K, class V> struct DictNode;
template <typename T>
DictNode<typename T::Key, T> *node_from_item(T *c) { return &c->node; }

template <class K, class V> struct DictNode
{
    K key;
    DictNode* left;
    DictNode* right;

    DictNode(K key): key(key), left(nullptr), right(nullptr) {}

    static DictNode *node(V *item) {
        return node_from_item(item);
    }
    static intptr_t node_offset() {
        return (intptr_t)node((V*)0);
    }
    V* item() {
        return (V*)((u8*)this - node_offset());
    }
};
template <class V, class K = typename V::Key> struct Dict
{
    typedef DictNode<K, V> Node;
    Node* root;

    Dict(): root(nullptr) {}

    V* find(K key) {
        Node *node = root;
        while (node) {
            if (node->key == key) {
                return node->item();
            }
            node = node->right;
        }
        return NULL;
    }

    V *insert(V* item) {
        Node *node = Node::node(item);
        node->right = root;
        root = node;
        return item;
    }

    void remove(V* item) {
        remove(Node::node(item)->key);
    }
    void remove(K key) {
        Node **p = &root;
        while (Node *node = *p) {
            if (node->key == key) {
                *p = node = node->right;
                return;
            }
            p = &node->right;
        }
        assert(!"Removing non-existing item");
    }
};

#define DICT_NODE(Class, member) \
    DictNode<Class::Key, Class> *node_from_item(Class *c) { return &c->member; }

namespace aspace {
enum MapFlags {
    MAP_X = 1 << 0,
    MAP_W = 1 << 1,
    MAP_R = 1 << 2,
    MAP_RX = MAP_R | MAP_X,
    MAP_RW = MAP_R | MAP_W,
    MAP_RWX = MAP_R | MAP_W | MAP_X,
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
struct Sharing;
struct Backing {
    typedef uintptr_t Key;
    DictNode<Key, Backing> node;
    union { uintptr_t paddr; Sharing *parent; } pp;
    DListNode<Backing> child_node;

    Backing(uintptr_t vaddrFlags, uintptr_t paddr_or_parent):
        node(vaddrFlags) {
        pp.paddr = paddr_or_parent;
    }

    static Backing* new_phys(uintptr_t vaddrFlags, uintptr_t paddr) {
        assert(!(paddr & 0xfff));
        return new Backing(vaddrFlags, paddr);
    }
    static Backing* new_anon(uintptr_t vaddrFlags) {
        return new Backing(vaddrFlags | MAP_PHYS, ToPhysAddr(new u8[4096]));
    }
//    static Backing* new_share(uintptr_t vadddrFlags, Sharing* share) {
//    }

    u64 vaddr() const {
        return node.key & -0x1000;
    }
    u16 flags() const {
        return node.key & 0xfff;
    }
    u64 paddr() const {
        return flags() & MAP_PHYS ? pp.paddr : 0 /*pp.parent->paddr()*/;
    }
    u64 pte() const {
        return paddr() | pte_flags();
    }
    u64 pte_flags() const {
        u64 pte = 5; // Present, User-accessible
        if (!(flags() & MAP_X)) {
            // Not executable, so set NX bit
            pte |= 1ull << 63;
        }
        if (flags() & MAP_W) {
            pte |= 2;
        }
        return pte;
    }
};
DLIST_NODE(Backing, child_node);

typedef u64 PageTable[512];
typedef PageTable PML4;

PML4 *allocate_pml4() {
    PML4 *ret = (PML4 *)new PML4;
    (*ret)[511] = start32::kernel_pdp_addr | 3;
    return ret;
}

PageTable *get_alloc_pt(PageTable table, u64 index, u16 flags) {
    index &= 0x1ff;
    u64 existing = table[index];
    if (existing & 1) {
        return PhysAddr<PageTable>(existing & -0x1000);
    } else {
        PageTable *res = (PageTable *)new PageTable;
        table[index] = ToPhysAddr(res) | flags;
        return res;
    }
}

struct AddressSpace {
    PML4 *pml4;
    u32 count;

    AddressSpace(): pml4(allocate_pml4()), count(1) {}

    Dict<MapCard> mapcards;
    Dict<Backing> backings;

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

    Backing& add_anon_backing(MapCard* card, uintptr_t vaddr) {
        return *backings.insert(Backing::new_anon(vaddr | card->flags()));
    }

    Backing& add_phys_backing(MapCard* card, uintptr_t vaddr) {
        return *backings.insert(Backing::new_phys(
            vaddr | card->flags(), card->paddr(vaddr)));
    }

    Backing& find_add_backing(uintptr_t vaddr) {
        if (auto back = backings.find(vaddr | 0xfff)) {
            return *back;
        }

        auto card = mapcards.find(vaddr);
        assert(card && "No mapping");
        assert((card->flags() & MAP_RWX) && "No access");
        if (card->handle) {
            unimpl("User mappings");
        }

        if ((card->flags() & MAP_DMA) == MAP_ANON) {
            return add_anon_backing(card, vaddr);
        } else if (card->flags() & MAP_PHYS) {
            return add_phys_backing(card, vaddr);
        } else {
            abort("not anon or phys for handle==0");
        }
    }

    void add_pte(uintptr_t vaddr, uintptr_t pte) {
        log(add_pte, "Mapping %p to %p\n", (void*)vaddr, (void*)pte);
        auto pdp = get_alloc_pt(*pml4, vaddr >> 39, 7);
        auto pd = get_alloc_pt(*pdp, vaddr >> 30, 7);
        auto pt = get_alloc_pt(*pd, vaddr >> 21, 7);
        (*pt)[(vaddr >> 12) & 0x1ff] = pte;
    }
};
}
