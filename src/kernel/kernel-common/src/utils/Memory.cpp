#include "Memory.hpp"

namespace kernel::common::memory {
	template<class T> void UniquePtr<T>::clear() {
		if (this->ptr != nullptr) {
			delete this->ptr;
		}

		this->ptr = nullptr;
	}

	template<class T> constexpr UniquePtr<T>::UniquePtr() noexcept: ptr(nullptr) {}

	template<class T> constexpr UniquePtr<T>::UniquePtr(nullptr_t) noexcept: ptr(nullptr) {}

	template<class T> constexpr UniquePtr<T>::UniquePtr(pointer p) noexcept: ptr(p) {}

	template<class T> template<class U> constexpr UniquePtr<T>::UniquePtr(UniquePtr<U> &&p) noexcept: ptr(p.ptr) {
		p.ptr = nullptr;
	}

	template<class T> UniquePtr<T>::~UniquePtr() {
		this->clear();
	}

	template<class T> template<class U> constexpr UniquePtr<T> &UniquePtr<T>::operator=(UniquePtr<U> &&r) noexcept {
		this->ptr = r.ptr;
		r.ptr = nullptr;

		return *this;
	}

	template<class T> constexpr typename UniquePtr<T>::pointer UniquePtr<T>::release() noexcept {
		pointer oldPtr = ptr;
		this->ptr = nullptr;

		return oldPtr;
	}

	template<class T> constexpr void UniquePtr<T>::reset(pointer p_ptr) noexcept {
		this->clear();

		this->ptr = p_ptr;
	}

	template<class T> constexpr void UniquePtr<T>::swap(UniquePtr &other) noexcept {
		pointer tmp = this->ptr;
		this->ptr = other.ptr;
		other.ptr = tmp;
	}

	template<class T> constexpr typename UniquePtr<T>::pointer UniquePtr<T>::get() const noexcept {
		return this->ptr;
	}

	template<class T> constexpr UniquePtr<T>::operator bool() const noexcept {
		return this->ptr != nullptr;
	}

	template<class T> constexpr T &UniquePtr<T>::operator*() const noexcept {
		return *this->ptr;
	}

	template<class T> constexpr T *UniquePtr<T>::operator->() const noexcept {
		return this->get();
	}

	template<class T> constexpr bool UniquePtr<T>::operator<(const UniquePtr &p) const noexcept {
		return this->get() < p.get();
	}

	template<class T> constexpr bool UniquePtr<T>::operator>(const UniquePtr &p) const noexcept {
		return this->get() > p.get();
	}

	template<class T> void UniquePtr<T[]>::clear() {
		if (this->ptr != nullptr) {
			delete[] this->ptr;
		}

		this->ptr = nullptr;
	}

	template<class T> constexpr UniquePtr<T[]>::UniquePtr() noexcept: ptr(nullptr) {}

	template<class T> constexpr UniquePtr<T[]>::UniquePtr(nullptr_t) noexcept: ptr(nullptr) {}

	template<class T> constexpr UniquePtr<T[]>::UniquePtr(pointer p) noexcept: ptr(p) {}

	template<class T> constexpr UniquePtr<T[]>::UniquePtr(UniquePtr &&p) noexcept: ptr(p.ptr) {
		p.ptr = nullptr;
	}

	template<class T> UniquePtr<T[]>::~UniquePtr() {
		this->clear();
	}

	template<class T> template<class U> constexpr UniquePtr<T> &UniquePtr<T[]>::operator=(UniquePtr<U> &&r) noexcept {
		this->clear();

		this->ptr = r.ptr;
		r.ptr = nullptr;

		return *this;
	}

	template<class T> UniquePtr<T[]> &UniquePtr<T[]>::operator=(UniquePtr &&r) noexcept {
		this->clear();

		this->ptr = r.ptr;
		r.ptr = nullptr;

		return *this;
	}

	template<class T> constexpr typename UniquePtr<T[]>::pointer UniquePtr<T[]>::release() noexcept {
		pointer oldPtr = ptr;
		this->ptr = nullptr;

		return oldPtr;
	}

	template<class T> constexpr void UniquePtr<T[]>::reset(pointer p_ptr) noexcept {
		this->clear();

		this->ptr = p_ptr;
	}

	template<class T> constexpr void UniquePtr<T[]>::swap(UniquePtr &other) noexcept {
		pointer tmp = this->ptr;
		this->ptr = other.ptr;
		other.ptr = tmp;
	}

	template<class T> constexpr typename UniquePtr<T[]>::pointer UniquePtr<T[]>::get() const noexcept {
		return this->ptr;
	}

	template<class T> constexpr UniquePtr<T[]>::operator bool() const noexcept {
		return this->ptr != nullptr;
	}

	template<class T> constexpr T &UniquePtr<T[]>::operator[](size_t element) const noexcept {
		return this->ptr[element];
	}

	template<class T, class... Args> constexpr UniquePtr<T> makeUnique(Args &&...args) {
		return UniquePtr(new T(forward<Args>(args)...));
	}

	template<class T> constexpr UniquePtr<T> makeUnique(size_t size) {
		return UniquePtr<T>(new typename UniquePtr<T>::type[size]());
	}

	template<class T> void SharedPtr<T>::clear() {
		if (this->refCount == nullptr)
			return;

		unsigned long tempSharedRefs = -1;
		unsigned long tempWeakRefs   = -1;

		this->refCount->s.lock();
		tempSharedRefs = --this->refCount->sharedRefs;
		tempWeakRefs = this->refCount->weakRefs;
		this->refCount->s.unlock();

		if (tempSharedRefs == 0) {
			delete this->ptr;

			if (tempWeakRefs == 0) {
				delete this->refCount;
			}
		}
	}

	template<class T> constexpr SharedPtr<T>::SharedPtr() noexcept: ptr(nullptr), refCount(nullptr) {}

	template<class T> constexpr SharedPtr<T>::SharedPtr(nullptr_t) noexcept: ptr(nullptr), refCount(nullptr) {}

	template<class T> template<typename Y> SharedPtr<T>::SharedPtr(const SharedPtr<Y> &p): ptr(p.ptr), refCount(p.refCount) {
		if (this->refCount != nullptr) {
			this->refCount->s.lock();
			this->refCount->sharedRefs += 1;
			this->refCount->s.unlock();
		}
	}

	template<class T> SharedPtr<T>::SharedPtr(const SharedPtr &p): ptr(p.ptr), refCount(p.refCount) {
		if (this->refCount != nullptr) {
			this->refCount->s.lock();
			this->refCount->sharedRefs += 1;
			this->refCount->s.unlock();
		}
	}

	template<class T> constexpr SharedPtr<T>::SharedPtr(SharedPtr &&p): ptr(p.ptr), refCount(p.refCount) {
		p.ptr = nullptr;
		p.refCount = nullptr;
	}

	template<class T> template<typename U> constexpr SharedPtr<T>::SharedPtr(SharedPtr<U> &&p): ptr(p.ptr), refCount(p.refCount) {
		p.ptr = nullptr;
		p.refCount = nullptr;
	}

	template<class T> SharedPtr<T>::~SharedPtr() {
		this->clear();
	}

	template<class T> SharedPtr<T> &SharedPtr<T>::operator=(const SharedPtr &r) noexcept {
		if (this == &r) {
			return *this;
		}

		this->clear();

		this->ptr = r.ptr;
		this->refCount = r.refCount;

		if (this->refCount != nullptr) {
			this->refCount->s.lock();
			this->refCount->sharedRefs += 1;
			this->refCount->s.unlock();
		}

		return *this;
	}

	template<class T> template<typename U> SharedPtr<T> &SharedPtr<T>::operator=(const SharedPtr<U> &r) noexcept {
		if (this == &r) {
			return *this;
		}

		this->clear();

		this->ptr = r.ptr;
		this->refCount = r.refCount;

		if (this->refCount != nullptr) {
			this->refCount->s.lock();
			this->refCount->sharedRefs += 1;
			this->refCount->s.unlock();
		}

		return *this;
	}

	template<class T> SharedPtr<T> &SharedPtr<T>::operator=(SharedPtr &&r) noexcept {
		if (this == &r) {
			return *this;
		}

		this->clear();

		this->ptr = r.ptr;
		this->refCount = r.refCount;

		r.ptr = nullptr;
		r.refCount = nullptr;

		return *this;
	}

	template<class T> template<typename U> SharedPtr<T> &SharedPtr<T>::operator=(SharedPtr<U> &&r) noexcept {
		if (this == &r)
			return *this;

		this->clear();

		this->ptr = r.ptr;
		this->refCount = r.refCount;

		r.ptr = nullptr;
		r.refCount = nullptr;

		return *this;
	}

	template<class T> template<typename U> SharedPtr<T> &SharedPtr<T>::operator=(UniquePtr<U> &&r) {
		UniquePtr<SmartPtrRefcountStr> newRefCount = nullptr;

		if (r) {
			newRefCount = makeUnique<SmartPtrRefcountStr>(SmartPtrRefcountStr());

			if (newRefCount)
				newRefCount->sharedRefs++;
			else
				r = nullptr;
		}

		this->clear();

		this->ptr = r.release();
		this->refCount = newRefCount.release();

		return *this;
	}

	template<class T> constexpr typename SharedPtr<T>::elementType *SharedPtr<T>::get() const noexcept {
		return this->ptr;
	}

	template<class T> constexpr T &SharedPtr<T>::operator*() const noexcept {
		return *this->ptr;
	}

	template<class T> constexpr T *SharedPtr<T>::operator->() const noexcept {
		return this->get();
	}

	template<class T> typename SharedPtr<T>::elementType &SharedPtr<T>::operator[](unsigned long idx) const {
		return this->get()[idx];
	}

	template<class T> long SharedPtr<T>::useCount() const noexcept {
		return this->refCount ? this->refCount->sharedRefs : 0;
	}

	template<class T> bool SharedPtr<T>::unique() const noexcept {
		return this->useCount() == 1;
	}

	template<class T> constexpr SharedPtr<T>::operator bool() const {
		return this->get() != nullptr;
	}

	template<class T, class U> SharedPtr<T> dynamicPointerCast(const SharedPtr<U> &sp) noexcept {
		if (sp.ptr == nullptr) {
			return nullptr;
		}

		T *ptr = dynamic_cast<T *>(sp.ptr);

		if (ptr == nullptr) {
			return nullptr;
		}

		SharedPtr<T> newPtr;

		newPtr.ptr = ptr;
		newPtr.refCount = sp.refCount;

		if (newPtr.refCount != nullptr) {
			newPtr.refCount->s.lock();
			newPtr.refCount->sharedRefs += 1;
			newPtr.refCount->s.unlock();
		}

		return newPtr;
	}

	template<class T, class U> SharedPtr<T> staticPointerCast(const SharedPtr<U> &sp) noexcept {
		if (sp.ptr == nullptr) {
			return nullptr;
		}

		T *ptr = static_cast<T *>(sp.ptr);

		SharedPtr<T> newPtr;

		newPtr.ptr = ptr;
		newPtr.refCount = sp.refCount;

		if (newPtr.refCount != nullptr) {
			newPtr.refCount->s.lock();
			newPtr.refCount->sharedRefs += 1;
			newPtr.refCount->s.unlock();
		}

		return newPtr;
	}

	template<class T, class... Args> SharedPtr<T> makeShared(Args &&...args) {
		return SharedPtr<T>(makeUnique<T>(forward<Args>(args)...));
	}

	template<class T, class U> auto operator==(const SharedPtr<T> &lhs, const SharedPtr<U> &rhs) noexcept {
		return lhs.get() == rhs.get();
	}

	template<class T, class U> auto operator!=(const SharedPtr<T> &lhs, const SharedPtr<U> &rhs) noexcept {
		return lhs.get() != rhs.get();
	}

	template<class T, class U> auto operator<(const SharedPtr<T> &lhs, const SharedPtr<U> &rhs) noexcept {
		return lhs.get() < rhs.get();
	}

	template<class T> void WeakPtr<T>::clear() {
		if (this->refCount == nullptr) {
			return;
		}

		bool deleteRefCount = false;

		this->refCount->s.lock();
		--this->refCount->weakRefs;

		if (this->refCount->sharedRefs == 0 and this->refCount->weakRefs == 0) {
			deleteRefCount = true;
		}

		this->refCount->s.unlock();

		if (deleteRefCount) {
			delete this->refCount;
		}
	}

	template<class T> WeakPtr<T>::WeakPtr(const WeakPtr &p) noexcept: ptr(p.ptr), refCount(p.refCount) {
		if (this->refCount != nullptr) {
			this->refCount->s.lock();
			++this->refCount->weakRefs;
			this->refCount->s.unlock();
		}
	}

	template<class T> WeakPtr<T>::WeakPtr(const SharedPtr<T> &p) noexcept: ptr(p.ptr), refCount(p.refCount) {
		if (this->refCount != nullptr) {
			this->refCount->s.lock();
			++this->refCount->weakRefs;
			this->refCount->s.unlock();
		}
	}

	template<class T> constexpr WeakPtr<T>::WeakPtr(WeakPtr &&r) noexcept: ptr(r.ptr), refCount(r.refCount) {
		r.ptr = nullptr;
		r.refCount = nullptr;
	}

	template<class T> WeakPtr<T>::~WeakPtr() {
		this->clear();
	}

	template<class T> WeakPtr<T> &WeakPtr<T>::operator=(const SharedPtr<T> &r) noexcept {
		this->clear();

		this->ptr = r.ptr;
		this->refCount = r.refCount;

		if (this->refCount != nullptr) {
			this->refCount->s.lock();
			this->refCount->weakRefs += 1;
			this->refCount->s.unlock();
		}

		return *this;
	}

	template<class T> constexpr WeakPtr<T> &WeakPtr<T>::operator=(WeakPtr &&r) noexcept {
		this->clear();

		ptr = r.ptr;
		this->refCount = r.refCount;

		r.ptr = nullptr;
		r.refCount = nullptr;

		return *this;
	}

	template<class T> long WeakPtr<T>::useCount() const noexcept {
		if (this->refCount == nullptr) {
			return 0;
		}

		return this->refCount->sharedRefs;
	}

	template<class T> bool WeakPtr<T>::expired() const noexcept {
		return this->useCount() == 0;
	}

	template<class T> SharedPtr<T> WeakPtr<T>::lock() const noexcept {
		if (this->refCount == nullptr) {
			return SharedPtr<T>();
		}

		SharedPtr<T> p = SharedPtr<T>();

		this->refCount->s.lock();

		if (this->refCount->sharedRefs) {
			++this->refCount->sharedRefs;
			p.ptr = ptr;
		}

		this->refCount->s.unlock();

		if (p.ptr != nullptr) {
			p.refCount = this->refCount;
		}

		return p;
	}

	template<class T> constexpr bool WeakPtr<T>::operator<(const WeakPtr &p) const {
		return this->ptr == p.ptr ? this->refCount < p.refCount : this->ptr < p.ptr;
	}

	template<class T> template<class U> bool WeakPtr<T>::operator==(const WeakPtr<U> &rhs) noexcept {
		return this->refCount == rhs.refCount;
	}

	template<class T> EnableSharedFromThis<T> &EnableSharedFromThis<T>::operator=(const EnableSharedFromThis &) noexcept {
		return *this;
	}

	template<class T> SharedPtr<T> EnableSharedFromThis<T>::sharedFromThis() {
		return this->weakThis.lock();
	}

	template<class T> SharedPtr<T const> EnableSharedFromThis<T>::sharedFromThis() const {
		return this->weakThis.lock();
	}

	template<class T> WeakPtr<T> EnableSharedFromThis<T>::weakFromThis() noexcept {
		return this->weakThis;
	}

	template<class T> WeakPtr<T const> EnableSharedFromThis<T>::weakFromThis() const noexcept {
		return this->weakThis;
	}

	template<class T> SharedPtr<T>::SharedPtr(UniquePtr<T> &&p) {
		if (not p.get())
			return;

		UniquePtr<SmartPtrRefcountStr> refCountNew = makeUnique<SmartPtrRefcountStr>(SmartPtrRefcountStr());

		if (not refCountNew)
			return;

		this->ptr = p.release();
		this->refCount = refCountNew.release();
		this->refCount->sharedRefs = 1;

		if constexpr (isBaseOfTemplate<EnableSharedFromThis, T>::value) {
			this->ptr->weakThis = *this;
		}
	}
}