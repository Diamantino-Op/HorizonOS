#ifndef KERNEL_COMMON_ELF_HPP
#define KERNEL_COMMON_ELF_HPP

#include "memory/VirtualAllocator.hpp"

#include "Types.hpp"

namespace kernel::common::programs {
    using namespace memory;

    # define ELF32_ST_BIND(INFO)	((INFO) >> 4)
    # define ELF32_ST_TYPE(INFO)	((INFO) & 0x0F)

    constexpr u8 ElfIdentitySize = 16;

    constexpr u8 ElfMagic0 = 0x7F;
    constexpr u8 ElfMagic1 = 'E';
    constexpr u8 ElfMagic2 = 'L';
    constexpr u8 ElfMagic3 = 'F';

    constexpr u8 Elf64Bit = 2;
    constexpr u8 Elf32Bit = 1;

    constexpr u8 ElfLSB = 1;
    constexpr u8 ElfMSB = 2;

    constexpr u16 ElfX86Machine = 62;
    constexpr u16 ElfArmMachine = 40;
	constexpr u16 ElfRiscVMachine = 243;

    constexpr u32 ElfCurrVersion = 1;

    constexpr u16 ShnUndefined = 0x00;
    constexpr u16 ShnAbsolute = 0xFFF1;
    constexpr u16 ShnCommon = 0xFFF2;

    typedef u64 Elf64Addr;
    typedef u32 Elf32Addr;

    typedef u64 Elf64Off;
    typedef u32 Elf32Off;

    typedef i64 ElfSXWord;
    typedef i32 ElfSWord;

    typedef u64 ElfXWord;
    typedef u32 ElfWord;

    typedef u16 ElfHalf;
    typedef u8 ElfByte;
    typedef u16 ElfSection;

    struct ElfCommonHeader {
        u8		    elfIdentity[ElfIdentitySize];
        ElfHalf	    elfType;
        ElfHalf	    elfMachine;
        ElfWord	    elfVersion;
    };

    struct Elf32Header {
        ElfCommonHeader commonHeader;
        Elf32Addr	    elfEntryAddr;
        Elf32Off	    elfProgHeaderOff;
        Elf32Off	    elfSectionHeaderOff;
        ElfWord	        elfFlags;
        ElfHalf	        elfHeaderSize;
        ElfHalf         elfProgHeaderSize;
        ElfHalf	        elfProgHeaderAmount;
        ElfHalf	        elfSectionHeaderSize;
        ElfHalf	        elfSectionHeaderAmount;
        ElfHalf	        elfStringTableSectionHeaderIndex;
    };

    struct Elf64Header {
        ElfCommonHeader commonHeader;
        Elf64Addr	    elfEntryAddr;
        Elf64Off	    elfProgHeaderOff;
        Elf64Off	    elfSectionHeaderOff;
        ElfWord	        elfFlags;
        ElfHalf	        elfHeaderSize;
        ElfHalf         elfProgHeaderSize;
        ElfHalf	        elfProgHeaderAmount;
        ElfHalf	        elfSectionHeaderSize;
        ElfHalf	        elfSectionHeaderAmount;
        ElfHalf	        elfStringTableSectionHeaderIndex;
    };

    struct Elf32SectionHeader {
        ElfWord	    name;
        ElfWord	    type;
        ElfWord	    flags;
        Elf32Addr	addr;
        Elf32Off	offset;
        ElfWord	    size;
        ElfWord	    link;
        ElfWord	    info;
        ElfWord	    addrAlign;
        ElfWord	    entSize;
    };

    struct Elf64SectionHeader {
        ElfWord	    name;
        ElfWord	    type;
        ElfXWord	flags;
        Elf64Addr	addr;
        Elf64Off	offset;
        ElfXWord	size;
        ElfWord	    link;
        ElfWord	    info;
        ElfXWord	addrAlign;
        ElfXWord	entSize;
    };

    struct Elf32SymbolTable {
        ElfWord		name;
        Elf32Addr	value;
        ElfWord		size;
        u8			info;
        u8			other;
        ElfHalf		sectionIndex;
    };

    struct Elf64SymbolTable {
        ElfWord		name;
        u8			info;
        u8			other;
        ElfHalf		sectionIndex;
        Elf64Addr	value;
        ElfXWord	size;
    };

    struct Elf32Rel {
        Elf32Addr   offset;
        ElfWord     info;
    };

    struct Elf32Rela {
        Elf32Addr   offset;
        ElfWord     info;
        ElfSWord    addend;
    };

    struct Elf64Rel {
        Elf64Addr   offset;
        ElfXWord    info;
    };

    struct Elf64Rela {
        Elf64Addr   offset;
        ElfXWord    info;
        ElfSXWord   addend;
    };

    struct Elf32ProgramHeader {
        ElfWord     type;
        Elf32Off    offset;
        Elf32Addr   vaddr;
        Elf32Addr   paddr;
        ElfWord     filesz;
        ElfWord     memsz;
        ElfWord     flags;
        ElfWord     align;
    };

    struct Elf64ProgramHeader {
        ElfWord     type;
        ElfWord     flags;
        Elf64Off    offset;
        Elf64Addr   vaddr;
        Elf64Addr   paddr;
        ElfXWord    filesz;
        ElfXWord    memsz;
        ElfXWord    align;
    };

    #define ELF32_R_SYM(i)    ((i) >> 8)
    #define ELF32_R_TYPE(i)   ((i) & 0xff)
    #define ELF64_R_SYM(i)    ((i) >> 32)
    #define ELF64_R_TYPE(i)   ((i) & 0xffffffff)

    // x86 32-bit relocations
    enum Elf32RelocationType {
        R_386_NONE          = 0,
        R_386_32            = 1,
        R_386_PC32          = 2,
        R_386_GOT32         = 3,
        R_386_PLT32         = 4,
        R_386_COPY          = 5,
        R_386_GLOB_DAT      = 6,
        R_386_JMP_SLOT      = 7,
        R_386_RELATIVE      = 8,
        R_386_GOTOFF        = 9,
        R_386_GOTPC         = 10
    };

    // x86-64 relocations
    enum Elf64RelocationType {
        R_X86_64_NONE       = 0,
        R_X86_64_64         = 1,
        R_X86_64_PC32       = 2,
        R_X86_64_GOT32      = 3,
        R_X86_64_PLT32      = 4,
        R_X86_64_COPY       = 5,
        R_X86_64_GLOB_DAT   = 6,
        R_X86_64_JUMP_SLOT  = 7,
        R_X86_64_RELATIVE   = 8,
        R_X86_64_GOTPCREL   = 9,
        R_X86_64_32         = 10,
        R_X86_64_32S        = 11,
        R_X86_64_16         = 12,
        R_X86_64_PC16       = 13,
        R_X86_64_8          = 14,
        R_X86_64_PC8        = 15
    };

    enum ProgramHeaderType {
        PT_NULL             = 0,
        PT_LOAD             = 1,
        PT_DYNAMIC          = 2,
        PT_INTERP           = 3,
        PT_NOTE             = 4,
        PT_SHLIB            = 5,
        PT_PHDR             = 6,
        PT_TLS              = 7
    };

    enum SymbolTableBinding {
        STB_LOCAL		= 0, // Local scope
        STB_GLOBAL		= 1, // Global scope
        STB_WEAK		= 2  // Weak, (ie. __attribute__((weak)))
    };

    enum SymbolTableType {
        STT_NOTYPE		= 0, // No type
        STT_OBJECT		= 1, // Variables, arrays, etc.
        STT_FUNC		= 2, // Methods or functions
        STT_SECTION     = 3, // Section
    };

    enum SectionHeaderType {
        SHT_NULL	    = 0,   // Null section
        SHT_PROGBITS	= 1,   // Program information
        SHT_SYMTAB	    = 2,   // Symbol table
        SHT_STRTAB	    = 3,   // String table
        SHT_RELA	    = 4,   // Relocation (w/ addend)
        SHT_HASH	    = 5,   // Symbol hash table
        SHT_DYNAMIC	    = 6,   // Dynamic linking information
        SHT_NOBITS	    = 8,   // Not present in file
        SHT_REL		    = 9,   // Relocation (no addend)
        SHT_DYNSYM	    = 11   // Dynamic linker symbol table
    };

    enum SectionHeaderAttribute {
        SHF_WRITE	= 0x01, // Writable section
        SHF_ALLOC	= 0x02,  // Exists in memory
        SHF_EXECINSTR = 0x04  // Executable section
    };

    enum ElfIdent {
        ELF_ID_MAG0		    = 0, // 0x7F
        ELF_ID_MAG1		    = 1, // 'E'
        ELF_ID_MAG2		    = 2, // 'L'
        ELF_ID_MAG3		    = 3, // 'F'
        ELF_ID_CLASS	    = 4, // Architecture (32/64)
        ELF_ID_DATA		    = 5, // Byte Order
        ELF_ID_VERSION	    = 6, // ELF Version
        ELF_ID_ABI	        = 7, // OS Specific
        ELF_ID_ABI_VERSION	= 8, // OS Specific
        ELF_ID_PADDING		= 9  // Padding
    };

    enum ElfType {
        ET_NONE		= 0, // Unknown Type
        ET_REL		= 1, // Relocatable File
        ET_EXEC		= 2, // Executable File
        ET_DYN      = 3, // Shared Object File
        ET_CORE     = 4  // Core File
    };

    class Elf {
    public:
        static u64 *loadElf(const u64 *elfFile, AllocContext *ctx, u64 baseAddr = 0);

        static bool isElf(const ElfCommonHeader *elfHeader);

    private:
        static u64 *loadRel(const u64 *elfFile, AllocContext *ctx, u64 baseAddr = 0);

        static u64 *loadExeDyn(const u64 *elfFile, AllocContext *ctx, u64 baseAddr = 0);

        static u64 *loadExe(const u64 *elfFile, AllocContext *ctx, u64 baseAddr = 0);

        static bool isSupported(const ElfCommonHeader *elfHeader);

        static bool is64Bit(const ElfCommonHeader *elfHeader);

        static bool is32Bit(const ElfCommonHeader *elfHeader);

        static Elf64SectionHeader *getElf64SectionHeader(const Elf64Header *elfHeader, u16 index);

        static Elf32SectionHeader *getElf32SectionHeader(const Elf32Header *elfHeader, u16 index);

        static char *elf64LookupString(Elf64Header *elfHeader, u64 offset);

        static char *elf32LookupString(Elf32Header *elfHeader, u64 offset);

        static u64 elf64GetSymValue(Elf64Header *elfHeader, u64 table, u64 idx);

        static u64 elf32GetSymValue(Elf32Header *elfHeader, u64 table, u64 idx);
    };
}

#endif
