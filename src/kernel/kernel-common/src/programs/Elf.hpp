#ifndef KERNEL_COMMON_ELF_HPP
#define KERNEL_COMMON_ELF_HPP

#include "memory/VirtualAllocator.hpp"

#include "Types.hpp"

namespace kernel::common::programs {
    using namespace memory;

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

        static bool isElf(const ElfCommonHeader *elfHeader);

        static bool isSupported(const ElfCommonHeader *elfHeader);

        static bool is64Bit(const ElfCommonHeader *elfHeader);

        static bool is32Bit(const ElfCommonHeader *elfHeader);

    private:
        static u64 *loadRel(const u64 *elfFile, AllocContext *ctx);

        static u64 *loadExeDyn(const u64 *elfFile, AllocContext *ctx);

        static u64 *loadExe(const u64 *elfFile, AllocContext *ctx);
    };
}

#endif