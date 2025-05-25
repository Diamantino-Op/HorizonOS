#include "UBSan.hpp"

#include "CommonMain.hpp"

using namespace kernel::common;

const char *kindToType(const u16 kind) {
    const char *type;

    switch(kind) {
        case 0:
    		type = "integer";
    		break;

        case 1:
    		type = "float";
    		break;

        default:
    		type = "unknown";
    		break;
    }

    return type;
}

unsigned int infoToBits(const u16 info) {
    return 1 << (info >> 1);
}

extern "C" {
	void __ubsan_handle_load_invalid_value(DataLoadInvalidValue *data, uPtr value) {
	    CommonMain::getTerminal()->warn("load_invalid_value @ %s:%u:%u {value: %#lx}", "UBSan", data->location.filename, data->location.line, data->location.column, value);
	}

	void __ubsan_handle_nonnull_arg(DataNonNullArg *data) {
	    CommonMain::getTerminal()->warn("handle_nonnull_arg @ %s:%u:%u {arg_index: %i}", "UBSan", data->location.filename, data->location.line, data->location.column, data->argIndex);
	}

	void __ubsan_handle_nullability_arg(DataNonNullArg *data) {
	    CommonMain::getTerminal()->warn("nullability_arg @ %s:%u:%u {arg_index: %i}", "UBSan", data->location.filename, data->location.line, data->location.column, data->argIndex);
	}

	void __ubsan_handle_nonnull_return_v1(DataOnlyLocation *data [[maybe_unused]], SourceLocation *location) {
	    CommonMain::getTerminal()->warn("nonnull_return @ %s:%u:%u", "UBSan", location->filename, location->line, location->column);
	}

	void __ubsan_handle_nullability_return_v1(DataOnlyLocation *data [[maybe_unused]], SourceLocation *location) {
	    CommonMain::getTerminal()->warn("nullability_return @ %s:%u:%u", "UBSan", location->filename, location->line, location->column);
	}

	void __ubsan_handle_vla_bound_not_positive(DataLocationType *data, uPtr bound) {
	    CommonMain::getTerminal()->warn(
	        "vla_bound_not_positive @ %s:%u:%u {bound: %#lx, type: %u-bit %s %s}",
	        "UBSan",
	        data->location.filename,
	        data->location.line,
	        data->location.column,
	        bound,
	        infoToBits(data->type->info),
	        kindToType(data->type->kind),
	        data->type->name);
	}

	void __ubsan_handle_add_overflow(DataLocationType *data, uPtr lhs, uPtr rhs) {
	    CommonMain::getTerminal()->warn(
	        "add_overflow @ %s:%u:%u {lhs: %#lx, rhs: %#lx, type: %u-bit %s %s}",
	        "UBSan",
	        data->location.filename,
	        data->location.line,
	        data->location.column,
	        lhs,
	        rhs,
	        infoToBits(data->type->info),
	        kindToType(data->type->kind),
	        data->type->name);
	}

	void __ubsan_handle_sub_overflow(DataLocationType *data, uPtr lhs, uPtr rhs) {
	    CommonMain::getTerminal()->warn(
	        "sub_overflow @ %s:%u:%u {lhs: %#lx, rhs: %#lx, type: %u-bit %s %s}",
	        "UBSan",
	        data->location.filename,
	        data->location.line,
	        data->location.column,
	        lhs,
	        rhs,
	        infoToBits(data->type->info),
	        kindToType(data->type->kind),
	        data->type->name);
	}

	void __ubsan_handle_mul_overflow(DataLocationType *data, uPtr lhs, uPtr rhs) {
	    CommonMain::getTerminal()->warn(
	        "mul_overflow @ %s:%u:%u {lhs: %#lx, rhs: %#lx, type: %u-bit %s %s}",
	        "UBSan",
	        data->location.filename,
	        data->location.line,
	        data->location.column,
	        lhs,
	        rhs,
	        infoToBits(data->type->info),
	        kindToType(data->type->kind),
	        data->type->name);
	}

	void __ubsan_handle_divrem_overflow(DataLocationType *data, uPtr lhs, uPtr rhs) {
	    CommonMain::getTerminal()->warn(
	        "divrem_overflow @ %s:%u:%u {lhs: %#lx, rhs: %#lx, type: %u-bit %s %s}",
	        "UBSan",
	        data->location.filename,
	        data->location.line,
	        data->location.column,
	        lhs,
	        rhs,
	        infoToBits(data->type->info),
	        kindToType(data->type->kind),
	        data->type->name);
	}

	void __ubsan_handle_negate_overflow(DataLocationType *data, uPtr old) {
	    CommonMain::getTerminal()->warn("negate_overflow @ %s:%u:%u {old: %#lx, type: %u-bit %s %s}", "UBSan", data->location.filename, data->location.line, data->location.column, old, infoToBits(data->type->info), kindToType(data->type->kind), data->type->name);
	}

	void __ubsan_handle_shift_out_of_bounds(DataShiftOutOfBounds *data, uPtr lhs, uPtr rhs) {
	    CommonMain::getTerminal()->warn(
	        "shift_out_of_bounds @ %s:%u:%u {lhs: %#lx, rhs: %#lx, rhs_type: %u-bit %s %s, lhs_type: %u-bit %s %s}",
	        "UBSan",
	        data->location.filename,
	        data->location.line,
	        data->location.column,
	        lhs,
	        rhs,
	        infoToBits(data->rhsType->info),
	        kindToType(data->rhsType->kind),
	        data->rhsType->name,
	        infoToBits(data->lhsType->info),
	        kindToType(data->lhsType->kind),
	        data->lhsType->name);
	}

	void __ubsan_handle_out_of_bounds(DataOutOfBounds *data, u64 index) {
	    CommonMain::getTerminal()->warn(
	        "out_of_bounds @ %s:%u:%u {index: %#lx, array_type: %u-bit %s %s, index_type: %u-bit %s %s}",
	        "UBSan",
	        data->location.filename,
	        data->location.line,
	        data->location.column,
	        index,
	        infoToBits(data->arrayType->info),
	        kindToType(data->arrayType->kind),
	        data->arrayType->name,
	        infoToBits(data->arrayType->info),
	        kindToType(data->arrayType->kind),
	        data->arrayType->name);
	}

	void __ubsan_handle_type_mismatch_v1(DataTypeMismatch *data, void *pointer) {
	    static const char *kind_strs[] = { "load of",     "store to",  "reference binding to",    "member access within", "member call on",      "constructor call on", "downcast of",
	                                       "downcast of", "upcast of", "cast to virtual base of", "nonnull binding to",   "dynamic operation on" };

	    if(pointer == nullptr) {
	        CommonMain::getTerminal()->warn("type_mismatch @ %s:%u:%u (%s nullptr pointer of type %s)", "UBSan", data->location.filename, data->location.line, data->location.column, kind_strs[data->typeCheckKind], data->type->name);
	    } else if ((1 << data->alignment) - 1) {
	        //CommonMain::getTerminal()->warn("type_mismatch @ %s:%u:%u (%s misaligned address %#lx of type %s)", "UBSan", data->location.filename, data->location.line, data->location.column, kind_strs[data->typeCheckKind], reinterpret_cast<uPtr>(pointer), data->type->name);
	    } else {
	        CommonMain::getTerminal()->warn(
	            "type_mismatch @ %s:%u:%u (%s address %#lx, not enough spce for type %s)",
	            "UBSan",
	            data->location.filename,
	            data->location.line,
	            data->location.column,
	            kind_strs[data->typeCheckKind],
	            reinterpret_cast<uPtr>(pointer),
	            data->type->name);
	    }
	}

	void __ubsan_handle_alignment_assumption(DataAlignmentAssumption *data, void *, void *, void *) {
	    CommonMain::getTerminal()->warn("alignment_assumption @ %s:%u:%u", "UBSan", data->location.filename, data->location.line, data->location.column);
	}

	void __ubsan_handle_implicit_conversion(DataImplicitConversion *data, void *, void *) {
	    CommonMain::getTerminal()->warn(
	        "implicit_conversion @ %s:%u:%u {from_type: %u-bit %s %s, to_type: %u-bit %s %s}",
	        "UBSan",
	        data->location.filename,
	        data->location.line,
	        data->location.column,
	        infoToBits(data->fromType->info),
	        kindToType(data->fromType->kind),
	        data->fromType->name,
	        infoToBits(data->toType->info),
	        kindToType(data->toType->kind),
	        data->toType->name);
	}

	void __ubsan_handle_invalid_builtin(DataInvalidBuiltin *data) {
	    CommonMain::getTerminal()->warn("invalid_builtin @ %s:%u:%u", "UBSan", data->location.filename, data->location.line, data->location.column);
	}

	void __ubsan_handle_pointer_overflow(DataOnlyLocation *data, void *, void *) {
	    CommonMain::getTerminal()->warn("pointer_overflow @ %s:%u:%u", "UBSan", data->location.filename, data->location.line, data->location.column);
	}

	[[noreturn]] void __ubsan_handle_builtin_unreachable(DataOnlyLocation *data) {
	    CommonMain::getTerminal()->error("builtin_unreachable @ %s:%u:%u", "UBSan", data->location.filename, data->location.line, data->location.column); // TODO: Use custom panic

		for (;;) {
			asm volatile("hlt");
		}
	}

	[[noreturn]] void __ubsan_handle_missing_return(DataOnlyLocation *data) {
	    CommonMain::getTerminal()->error("missing_return @ %s:%u:%u", "UBSan", data->location.filename, data->location.line, data->location.column); // TODO: Use custom panic

		for (;;) {
			asm volatile("hlt");
		}
	}

	void __ubsan_handle_function_type_mismatch(DataFunctionTypeMismatch *data, [[maybe_unused]] void *value) {
	    CommonMain::getTerminal()->warn("function type mismatch @ %s:%u:%u", "UBSan", data->location.filename, data->location.line, data->location.column);
	}
}