namespace mboot {

struct VBE {
    u32 control_info;
    u32 mode_info;
    u16 mode;
    u16 iface_seg;
    u16 iface_off;
    u16 iface_len;
};

struct FB {
    u64 addr;
    u32 pitch;
    u32 width;
    u32 height;
    u8 bpp;
    u8 fbtype;
    u8 colors[6];
};

enum FBType {
    Indexed = 0,
    RGB = 1,
    Text = 2,
};

struct FBPalette {
    u32 addr;
    u16 count;
};

struct FBPixelFormat {
    u8 red_shift;
    u8 red_mask;
    u8 green_shift;
    u8 green_mask;
    u8 blue_shift;
    u8 blue_mask;
};

enum InfoFlags {
    MemorySize = 1,
    BootDevice = 2,
    CommandLine = 4,
    Modules = 8,
    // There are two formats for the symbol bit of the info struct, not sure
    // what they are though.
    Symbols = 16 | 32,
    Symbols1 = 16,
    Symbols2 = 32,
    MemoryMap = 64,
    Drives = 128,
    ConfigTable = 256,
    LoaderName = 512,
    APMTable = 1024,
    VBEInfo = 2048
};

struct Info {
    u32 flags;
// if has(MemorySize)
    u32 mem_lower;
    u32 mem_upper;
// if has(BootDevice)
    u32 boot_devices;

// if has(CommandLine)
    u32 cmdline;

// if has(Modules)
    u32 mods_count;
    u32 mods_addr;

// 
    u32 syms[4];

// if has(MemoryMap)
    u32 mmap_length;
    u32 mmap_addr;

//
    u32 drives_length;
    u32 drives_addr;

    u32 config_table;

    u32 boot_loader;
    u32 apm_table;

    VBE vbe;
    FB fb;

    bool has(InfoFlags flag) const { return !!(flags & flag); }

    /*slice<Module> modules() {
        return make_slice(PhysAddr<Module>(mods_addr), mods_count);
    }*/
};

struct Module {
    u32 start;
    u32 end;
    u32 string;
    u32 reserved;
};

struct MemoryMapItem {
    // Size of item (bytes), *not* including the item_size field
    u32 item_size;
    u64 start;
    u64 length;
    // See values from MemoryTypes
    u32 item_type;
} __attribute__((packed));

enum MemoryTypes {
    MemoryTypeMemory = 1,
    MemoryTypeReserved = 2,
    MemoryTypeACPIRCL = 3,
    MemoryTypeACPISomething = 4,
};

}
