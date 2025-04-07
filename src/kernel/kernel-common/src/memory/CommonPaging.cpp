#include "CommonPaging.hpp"

namespace kernel::common::memory {
	TLBShootdownContext::~TLBShootdownContext() {
		finalize();
	}

	auto TLBShootdownContext::iterateOverPages() {
		return Iter<&TLBShootdownContext::pages, &TLBShootdownContext::pagesCount> {
			*this
		};
	}

	auto TLBShootdownContext::iterateOverRanges() {
		return Iter<&TLBShootdownContext::ranges, &TLBShootdownContext::rangesCount> {
			*this
		};
	}

	const PageTable *TLBShootdownContext::contextFor() const {
		return pageTable;
	}
}