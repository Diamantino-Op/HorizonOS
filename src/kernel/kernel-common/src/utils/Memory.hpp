#ifndef KERNEL_COMMON_MEMORY_HPP
#define KERNEL_COMMON_MEMORY_HPP

#include "Utility.hpp"
#include "threads/SpinLock.hpp"

#include "stddef.h"

namespace kernel::common::memory {
    using namespace threads;

    template<class T, class... Args> constexpr T *constructAt(T *p, Args &&...args);

    template<class T> class WeakPtr;

    template<class T> class UniquePtr;

    template<class T> class SharedPtr;

    template<class T> class UniquePtr {
        template<typename U> friend class UniquePtr;

    private:
        T *ptr = nullptr;

        void clear();

    public:
        typedef T *pointer;
        typedef T element_type;

        constexpr UniquePtr() noexcept;
        UniquePtr(const UniquePtr &) = delete;

        constexpr UniquePtr(nullptr_t) noexcept;

        constexpr UniquePtr(pointer p) noexcept;

        template<class U> constexpr UniquePtr(UniquePtr<U> &&p) noexcept;

        ~UniquePtr();

        UniquePtr operator=(const UniquePtr &) noexcept = delete;

        template<class U> constexpr UniquePtr &operator=(UniquePtr<U> &&r) noexcept;

        constexpr pointer release() noexcept;

        constexpr void reset(pointer p_ptr = pointer()) noexcept;

        constexpr void swap(UniquePtr &other) noexcept;

        constexpr pointer get() const noexcept;

        constexpr explicit operator bool() const noexcept;

        constexpr T &operator*() const noexcept;

        constexpr T *operator->() const noexcept;

        constexpr bool operator<(const UniquePtr &p) const noexcept;

        constexpr bool operator>(const UniquePtr &p) const noexcept;
    };

    template<class T> class UniquePtr<T[]> {
        template<typename U> friend class UniquePtr;

    private:
        T *ptr;

        void clear();

    public:
        typedef T *pointer;
        typedef T element_type;

        constexpr UniquePtr() noexcept;
        constexpr UniquePtr(nullptr_t) noexcept;

        explicit constexpr UniquePtr(pointer p) noexcept;

        constexpr UniquePtr(UniquePtr &&p) noexcept;

        ~UniquePtr();

        template<class U> constexpr UniquePtr<T> &operator=(UniquePtr<U> &&r) noexcept;

        UniquePtr &operator=(UniquePtr &&r) noexcept;

        constexpr pointer release() noexcept;

        constexpr void reset(pointer p_ptr = pointer()) noexcept;

        constexpr void swap(UniquePtr &other) noexcept;

        constexpr pointer get() const noexcept;

        constexpr explicit operator bool() const noexcept;

        constexpr T &operator[](size_t element) const noexcept;
    };

    template<class T, class... Args> constexpr UniquePtr<T> makeUnique(Args &&...args);

    template<class T> constexpr UniquePtr<T> makeUnique(size_t size);

    struct SmartPtrRefcountStr {
        unsigned long sharedRefs = 0;
        unsigned long weakRefs   = 0;
        SpinLock s;
    };

    template<class T> class SharedPtr {
        template<class U> friend class SharedPtr;

    private:
        T *ptr = nullptr;
        SmartPtrRefcountStr *refCount = nullptr;

        void clear();

    public:
        typedef T elementType;

        constexpr SharedPtr() noexcept;
        constexpr SharedPtr(nullptr_t) noexcept;

        template<typename Y> SharedPtr(const SharedPtr<Y> &p);

        SharedPtr(const SharedPtr &p);

        constexpr SharedPtr(SharedPtr &&p);

        template<typename U> constexpr SharedPtr(SharedPtr<U> &&p);

        SharedPtr(UniquePtr<T> &&p);

        ~SharedPtr();

        SharedPtr &operator=(const SharedPtr &r) noexcept;

        template<typename U> SharedPtr &operator=(const SharedPtr<U> &r) noexcept;

        SharedPtr &operator=(SharedPtr &&r) noexcept;

        template<typename U> SharedPtr &operator=(SharedPtr<U> &&r) noexcept;

        template<typename U> SharedPtr &operator=(UniquePtr<U> &&r);

        constexpr elementType *get() const noexcept;

        constexpr T &operator*() const noexcept;

        constexpr T *operator->() const noexcept;

        elementType &operator[](unsigned long idx) const;

        long useCount() const noexcept;

        bool unique() const noexcept;

        constexpr explicit operator bool() const;

        friend class WeakPtr<T>;

        template<class... Args> friend SharedPtr<T> makeShared(Args &&...args);

        template<class A, class U> friend SharedPtr<A> dynamicPointerCast(const SharedPtr<U> &sp) noexcept;

        template<class A, class U> friend SharedPtr<A> staticPointerCast(const SharedPtr<U> &sp) noexcept;
    };

    template<class T, class U> SharedPtr<T> dynamicPointerCast(const SharedPtr<U> &sp) noexcept;

    template<class T, class U> SharedPtr<T> staticPointerCast(const SharedPtr<U> &sp) noexcept;

    template<class T, class... Args> SharedPtr<T> makeShared(Args &&...args);

    template<class T, class U> auto operator==(const SharedPtr<T> &lhs, const SharedPtr<U> &rhs) noexcept;

    template<class T, class U> auto operator!=(const SharedPtr<T> &lhs, const SharedPtr<U> &rhs) noexcept;

    template<class T, class U> auto operator<(const SharedPtr<T> &lhs, const SharedPtr<U> &rhs) noexcept;

    template<class T> class WeakPtr {
        template<typename U> friend class WeakPtr;

    private:
        void clear();

    public:
        constexpr WeakPtr() noexcept = default;

        WeakPtr(const WeakPtr &p) noexcept;

        WeakPtr(const SharedPtr<T> &p) noexcept;

        constexpr WeakPtr(WeakPtr &&r) noexcept;

        ~WeakPtr();

        WeakPtr &operator=(const WeakPtr &r) noexcept;

        WeakPtr &operator=(const SharedPtr<T> &r) noexcept;

        constexpr WeakPtr &operator=(WeakPtr &&r) noexcept;

        void reset() noexcept;
        void swap(WeakPtr &r) noexcept;

        long useCount() const noexcept;

        bool expired() const noexcept;

        SharedPtr<T> lock() const noexcept;

        constexpr bool operator<(const WeakPtr &p) const;

        template<class U> bool operator==(const WeakPtr<U> &rhs) noexcept;

    private:
        T *ptr = nullptr;
        SmartPtrRefcountStr *refCount = nullptr;
    };

    template<class T> class EnableSharedFromThis
    {
    private:
        // TODO: Weak_ptr is reduntant as *this == ptr anyways
        WeakPtr<T> weakThis = WeakPtr<T>();

    protected:
        constexpr EnableSharedFromThis() noexcept = default;
        EnableSharedFromThis(const EnableSharedFromThis<T> &obj) noexcept = default;
        EnableSharedFromThis(EnableSharedFromThis<T> &&obj) noexcept = delete;
        ~EnableSharedFromThis() = default;

        EnableSharedFromThis<T> &operator=(const EnableSharedFromThis<T> &) noexcept
        {
            return *this;
        }

    public:
        SharedPtr<T> sharedFromThis() { return weakThis.lock(); }
        SharedPtr<T const> sharedFromThis() const { return weakThis.lock(); }
        WeakPtr<T> weakFromThis() noexcept { return weakThis; }
        WeakPtr<T const> weakFromThis() const noexcept { return weakThis; }

        template<class X> friend class SharedPtr;
    };

    template<class T> SharedPtr<T>::SharedPtr(UniquePtr<T> &&p)
    {
        if (not p.get())
            return;

        UniquePtr<SmartPtrRefcountStr> refCountNew = makeUnique<SmartPtrRefcountStr>(SmartPtrRefcountStr());

        if (not refCountNew)
            return;

        ptr = p.release();
        refCount = refCountNew.release();
        refCount->sharedRefs = 1;

        if constexpr (isBaseOfTemplate<EnableSharedFromThis, T>::value) {
            ptr->weakThis = *this;
        }
    }
}

#endif