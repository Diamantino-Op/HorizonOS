#ifndef KERNEL_COMMON_ELF_HPP
#define KERNEL_COMMON_ELF_HPP

#include "memory/VirtualAllocator.hpp"

#include "Types.hpp"

namespace kernel::common::programs {
    using namespace memory;

    # define ELF32_ST_BIND(INFO)	((INFO) >> 4)
    # define ELF32_ST_TYPE(INFO)	((INFO) & 0x0F)

    constexpr u8 ElfIdentitySize = 16;

    constexpr u8 ElfMagic0 = 0x07;
    constexpr u8 ElfMagic1 = 'E';
    constexpr u8 ElfMagic2 = 'L';
    constexpr u8 ElfMagic3 = 'F';

    constexpr u8 Elf64Bit = 2;
    constexpr u8 Elf32Bit = 1;

    constexpr u8 ElfLSB = 1;
    constexpr u8 ElfMSB = 2;

    constexpr u16 ElfX86Machine = 3;
    constexpr u16 ElfArmMachine = 40;

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
        static u64 *loadElf(const u64 *elfFile, AllocContext *ctx);

    private:
        static u64 *loadRel(const u64 *elfFile, AllocContext *ctx);

        static u64 *loadExeDyn(const u64 *elfFile, AllocContext *ctx);

        static u64 *loadExe(const u64 *elfFile, AllocContext *ctx);

        static bool isElf(const ElfCommonHeader *elfHeader);

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