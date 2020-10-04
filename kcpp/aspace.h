#include "ftable.h"

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
    MAP_NOCACHE = 1 << 5,
    MAP_DMA = MAP_ANON | MAP_PHYS,
    MAP_USER = MAP_NOCACHE | MAP_DMA | MAP_RWX,
};
struct MapCard {
    typedef uintptr_t Key;
    DictNode<Key, MapCard> as_node;
    // Should this be the actual file instead? At one point it was decided that
    // memory mappings should be "dumb" and only applied when a fault happens,
    // but I think POSIX semantics might be impossible with that...
    // In POSIXy terms, the file description is mapped, not the file descriptor.
    int fd;
    // .vaddr + .offset = handle-offset to be sent to backer on fault
    // For a direct physical mapping, paddr = .vaddr + .offset
    // .offset = handle-offset - vaddr
    // .offset = paddr - .vaddr
    // The low 12 bits contain flags.
    uintptr_t offset;

    MapCard(uintptr_t vaddr, int fd, uintptr_t offset):
        as_node(vaddr),
        fd(fd),
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
    uintptr_t offsetFlags(uintptr_t vaddr) const {
        return vaddr + offset;
    }
    void set(int fd, uintptr_t offset) {
        this->fd = fd;
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
    static Backing* new_shared(uintptr_t vaddrFlags, Sharing* share) {
        return new Backing(vaddrFlags, (uintptr_t)share);
    }

    u64 vaddr() const {
        return node.key & -0x1000;
    }
    u16 flags() const {
        return node.key & 0xfff;
    }
    u64 paddr() const;
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
            pte |= 1 << 1;
        }
        if (flags() & MAP_NOCACHE) {
            pte |= 1 << 4;
        }
        return pte;
    }
};
DLIST_NODE(Backing, child_node);
struct Sharing {
    // Key is virtual address
    typedef uintptr_t Key;
    DictNode<Key, Sharing> node;
    uintptr_t paddr;
    // Hmm?
    //AddressSpace *aspace;
    DList<Backing> children;

    Sharing(uintptr_t vaddr, uintptr_t paddr): node(vaddr), paddr(paddr) {
    }

    Backing *new_backing(uintptr_t vaddrFlags) {
        return children.append(Backing::new_shared(vaddrFlags, this));
    }
};
u64 Backing::paddr() const {
    return flags() & MAP_PHYS ? pp.paddr : pp.parent->paddr;
}

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

class AddressSpace: public RefCounted<AddressSpace> {
    PML4 *pml4;

    Dict<MapCard> mapcards;
    Dict<Backing> backings;
    Dict<Sharing> sharings;

    // Processes waiting for this address space to do something.
    DList<Process> waiters;
    // Processes in this address space waiting for something to happen, e.g. in
    // an open-ended receive that could be fulfilled by any other process.
    DList<Process> blocked;

    FTable files;
    // TODO Add back this for more efficient pending-pulse handling? Dict<PendingPulse> pending;

    char name_[16];

public:
    AddressSpace(): pml4(allocate_pml4()) {
        //snprintf(name_, sizeof(name_), "%p", this);
        name_[0] = '\0';
    }

    void set_name(const char *newname) {
        strlcpy(name_, newname, sizeof(name_));
    }
    const char *name() const { return name_; }

    void mapcard_set(uintptr_t vaddr, uintptr_t handle, uintptr_t offsetFlags) {
        if (MapCard *p = mapcards.find_exact(vaddr)) {
            p->set(handle, offsetFlags);
        } else {
            mapcards.insert(new MapCard(vaddr, handle, offsetFlags));
        }
    }
    void mapcard_set(uintptr_t vaddr, uintptr_t handle, intptr_t offset, int flags) {
        mapcard_set(vaddr, handle, offset | flags);
    }

    void map_range(uintptr_t start, uintptr_t end, int fd, uintptr_t offsetFlags) {
        log(map_range, "map_range %#lx..%#lx to %d:%#lx\n", start, end, fd, offsetFlags);

        // Start by figuring out what to do with the end of the range. We want
        // to make sure that end.. is mapped to whatever it was mapped to
        // before, and also optimize away any unnecessary cards.

        uintptr_t end_offset = 0;
        int end_fd = -1;
        MapCard *endCard = mapcards.find_le(end);
        if (endCard) {
            end_fd = endCard->fd;
            end_offset = endCard->offset;
        }
        if (end_offset == offsetFlags && end_fd == fd) {
            // Remove end card, it's equivalent to the start-card we're adding.
            // TODO Seems to be missing a few cases here... If end-vaddr <
            // start, we shouldn't touch it. (and we shouldn't add a start
            // handle either.)
            delete mapcards.remove(end);
        } else {
            // If the vaddr is less than our end, it's either inside the range
            // and will be removed below, or it's before the range and needs
            // to be duplicated at the end. If the vaddr is exactly equal, we
            // can just keep it to mark the end of the range.
            if (endCard->vaddr() != end) {
                mapcard_set(end, end_fd, end_offset);
            }
        }

        // Set this last, since there might be an older mapcard at vaddr==start
        // that sets what the parameters starting at end.
        mapcard_set(start, fd, offsetFlags);

        // Find all cards vaddr < key < end and remove them.
        while (MapCard *p = mapcards.remove_range_exclusive(start, end))
        {
            delete p;
        }
    }

    u64 cr3() const {
        return ToPhysAddr(pml4);
    }

    Backing* add_anon_backing(MapCard* card, uintptr_t vaddr) {
        return backings.insert(Backing::new_anon(vaddr | card->flags()));
    }

    Backing* add_phys_backing(MapCard* card, uintptr_t vaddr) {
        return backings.insert(Backing::new_phys(
            vaddr | card->flags(), card->paddr(vaddr)));
    }

    Backing& add_shared_backing(uintptr_t vaddrFlags, Sharing *sharing) {
        return *backings.insert(sharing->new_backing(vaddrFlags));
    }

    Backing* find_backing(uintptr_t vaddr) {
        auto back = backings.find_le(vaddr | 0xfff);
        if (back && back->vaddr() == (vaddr & -0x1000)) {
            // FIXME Not page_fault
            log(page_fault, "Found existing backing for %#lx at %#lx -> %#lx\n", vaddr, back->vaddr(), back->paddr());
            return back;
        }
        return nullptr;
    }

    Backing* find_add_backing(uintptr_t vaddr) {
        if (auto back = find_backing(vaddr)) {
            return back;
        }

        auto card = mapcards.find_le(vaddr);
        if (!card) {
            return nullptr;
        }
        assert(card->vaddr() <= vaddr);
        assert((card->flags() & MAP_RWX) && "No access");
        if (card->fd >= 0) {
            unimpl("User mappings");
        }

        if ((card->flags() & MAP_DMA) == MAP_ANON) {
            log(page_fault, "New anonymous backing for %#lx\n", vaddr);
            return add_anon_backing(card, vaddr);
        } else if (card->flags() & MAP_PHYS) {
            log(page_fault, "New physical backing for %#lx -> %#lx\n", vaddr, card->paddr(vaddr));
            return add_phys_backing(card, vaddr);
        } else {
            abort("not anon or phys for handle==0");
        }
    }

    Sharing *find_add_sharing(uintptr_t vaddr, uintptr_t paddr) {
        if (auto share = sharings.find_exact(vaddr)) {
            return share;
        }

        return sharings.insert(new Sharing(vaddr, paddr));
    }

    bool find_mapping(uintptr_t vaddr, uintptr_t& offsetFlags, int& fd) {
        auto card = mapcards.find_le(vaddr);
        if (!card) return false;

        offsetFlags = card->offsetFlags(vaddr);
        fd = card->fd;

        return true;
    }

    void add_pte(uintptr_t vaddr, uintptr_t pte) {
        log(add_pte, "Mapping %p to %p\n", (void*)vaddr, (void*)pte);
        auto pdp = get_alloc_pt(*pml4, vaddr >> 39, 7);
        auto pd = get_alloc_pt(*pdp, vaddr >> 30, 7);
        auto pt = get_alloc_pt(*pd, vaddr >> 21, 7);
        (*pt)[(vaddr >> 12) & 0x1ff] = pte;
    }

    int add_file(const RefCnt<File>& f) {
        return files.add_file(f);
    }
    RefCnt<File> get_file(int fd) const {
        return files.get_file(fd);
    }
    RefCnt<Socket> get_socket(int fd) {
        if (auto file = files.get_file(fd))
            return file->get_socket();

        return nullptr;
    }
    RefCnt<File>& file_at(int fd) {
        return files.at(fd);
    }
    int get_file_number(const RefCnt<File>& file) const {
        return files.get_file_number(file);
    }
    int get_num_files() const {
        return files.get_num_files();
    }
};
}
