#ifndef KERNEL_COMMON_UBSAN_HPP
#define KERNEL_COMMON_UBSAN_HPP

#include "Types.hpp"

struct SourceLocation {
    const char *filename;
    u32 line;
    u32 column;
};

struct TypeDescriptor {
    u16 kind;
    u16 info;
    char name[];
};

struct DataOnlyLocation {
    SourceLocation location;
};

struct DataLocationType {
    SourceLocation location;
    TypeDescriptor *type;
};

struct DataLoadInvalidValue {
    SourceLocation location;
    TypeDescriptor *type;
};

struct DataNonNullArg {
    SourceLocation location;
    SourceLocation attrLocation;
    int argIndex;
};

struct DataShiftOutOfBounds {
    SourceLocation location;
    TypeDescriptor *lhsType;
    TypeDescriptor *rhsType;
};

struct DataOutOfBounds {
    SourceLocation location;
    TypeDescriptor *arrayType;
    TypeDescriptor *indexType;
};

struct DataTypeMismatch {
    SourceLocation location;
    TypeDescriptor *type;
    u8 alignment;
    u8 typeCheckKind;
};

struct DataAlignmentAssumption {
    SourceLocation location;
    SourceLocation assumptionLocation;
    TypeDescriptor *type;
};

struct DataImplicitConversion {
    SourceLocation location;
    TypeDescriptor *fromType;
    TypeDescriptor *toType;
    u8 kind;
};

struct DataInvalidBuiltin {
    SourceLocation location;
    u8 kind;
};

struct DataFunctionTypeMismatch {
    SourceLocation location;
    TypeDescriptor *type;
};

#endif