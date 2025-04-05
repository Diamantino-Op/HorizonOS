#ifndef KERNEL_COMMON_MEMORY_HPP
#define KERNEL_COMMON_MEMORY_HPP

#include "Utility.hpp"
#include "stddef.h"

namespace kernel::common {
    template<class T, class... Args> constexpr T *constructAt(T *p, Args &&...args);

    template<class T> class WeakPtr;

    template<class T> class UniquePtr;

    template<class T> class SharedPtr;

    template<class T> class UniquePtr
    {
        template<typename U> friend class UniquePtr;

    private:
        T *ptr = nullptr;
        void _clear() {
            if (ptr != nullptr)
                delete ptr;

            ptr = nullptr;
        }

    public:
        typedef T *pointer;
        typedef T element_type;

        constexpr UniquePtr() noexcept: ptr(nullptr) {};
        UniquePtr(const UniquePtr &) = delete;

        constexpr UniquePtr(nullptr_t) noexcept: ptr(nullptr) {};

        constexpr UniquePtr(pointer p) noexcept: ptr(p) {};

        template<class U> constexpr UniquePtr<T>(UniquePtr<U> &&p) noexcept: ptr(p.ptr) {
            p.ptr = nullptr;
        }

        ~UniquePtr() {
            _clear();
        }

        UniquePtr operator=(const UniquePtr &) noexcept = delete;

        template<class U> constexpr UniquePtr<T> &operator=(UniquePtr<U> &&r) noexcept {
            ptr   = r.ptr;
            r.ptr = nullptr;
            return *this;
        }

        constexpr pointer release() noexcept {
            pointer oldptr = ptr;
            ptr = nullptr;
            return oldptr;
        }

        constexpr void reset(pointer p_ptr = pointer()) noexcept {
            _clear();
            this->ptr = p_ptr;
        }

        constexpr void swap(UniquePtr &other) noexcept {
            pointer tmp = this->ptr;
            this->ptr   = other->ptr;
            other->ptr  = tmp;
        }

        constexpr pointer get() const noexcept { return this->ptr; }

        constexpr explicit operator bool() const noexcept { return this->ptr != nullptr; }

        constexpr T &operator*() const noexcept { return *ptr; }

        constexpr T *operator->() const noexcept { return get(); }

        constexpr bool operator<(const UniquePtr &p) const noexcept { return this->get() < p.get(); }

        constexpr bool operator>(const UniquePtr &p) const noexcept { return this->get() > p.get(); }
    };

    template<class T> class UniquePtr<T[]>
    {
        template<typename U> friend class UniquePtr;

    private:
        T *ptr;

        void _clear()
        {
            if (ptr != nullptr)
                delete[] ptr;

            ptr = nullptr;
        }
    public:
        typedef T *pointer;
        typedef T element_type;

        constexpr unique_ptr() noexcept: ptr(nullptr) {};
        constexpr unique_ptr(nullptr_t) noexcept: ptr(nullptr) {};

        explicit constexpr unique_ptr(pointer p) noexcept: ptr(p) {};

        constexpr unique_ptr(unique_ptr &&p) noexcept: ptr(p.ptr) { p.ptr = nullptr; }

        ~unique_ptr()
        {
            _clear();
        }

        template<class U> constexpr unique_ptr<T> &operator=(unique_ptr<U> &&r) noexcept
        {
            _clear();

            ptr   = r.ptr;
            r.ptr = nullptr;
            return *this;
        }

        unique_ptr &operator=(unique_ptr &&r) noexcept
        {
            _clear();

            ptr   = r.ptr;
            r.ptr = nullptr;
            return *this;
        }

        constexpr pointer release() noexcept
        {
            pointer oldptr = ptr;
            ptr            = nullptr;
            return oldptr;
        }

        constexpr void reset(pointer p_ptr = pointer()) noexcept
        {
            _clear();
            this->ptr = p_ptr;
        }

        constexpr void swap(unique_ptr &other) noexcept
        {
            pointer tmp = this->ptr;
            this->ptr   = other->ptr;
            other->ptr  = tmp;
        }

        constexpr pointer get() const noexcept { return this->ptr; }

        constexpr explicit operator bool() const noexcept { return this->ptr != nullptr; }

        constexpr T &operator[](size_t element) const noexcept { return ptr[element]; }
    };

    template<class T, class... Args> constexpr unique_ptr<T> make_unique(Args &&...args)
    {
        return unique_ptr(new T(forward<Args>(args)...));
    }

    template<class T> constexpr unique_ptr<T> make_unique(size_t size)
    {
        return unique_ptr<T>(new typename remove_extent<T>::type[size]());
    }

    struct _smart_ptr_refcount_str {
        unsigned long shared_refs = 0;
        unsigned long weak_refs   = 0;
        Spinlock s;
    };

    template<class T> class shared_ptr
    {
        template<class U> friend class shared_ptr;

    private:
        T *ptr                            = nullptr;
        _smart_ptr_refcount_str *refcount = nullptr;

        void _clear()
        {
            if (refcount == nullptr)
                return;

            unsigned long temp_shared_refs = -1;
            unsigned long temp_weak_refs   = -1;

            refcount->s.lock();
            temp_shared_refs = --refcount->shared_refs;
            temp_weak_refs   = refcount->weak_refs;
            refcount->s.unlock();

            if (temp_shared_refs == 0) {
                delete ptr;

                if (temp_weak_refs == 0) {
                    delete refcount;
                }
            }
        }
    public:
        typedef T element_type;

        constexpr shared_ptr() noexcept: ptr(nullptr), refcount(nullptr) {};
        constexpr shared_ptr(nullptr_t) noexcept: ptr(nullptr), refcount(nullptr) {};

        template<typename Y> shared_ptr(const shared_ptr<Y> &p): ptr(p.ptr), refcount(p.refcount)
        {
            if (refcount != nullptr) {
                refcount->s.lock();
                refcount->shared_refs += 1;
                refcount->s.unlock();
            }
        }

        shared_ptr(const shared_ptr &p): ptr(p.ptr), refcount(p.refcount)
        {
            if (refcount != nullptr) {
                refcount->s.lock();
                refcount->shared_refs += 1;
                refcount->s.unlock();
            }
        }

        constexpr shared_ptr(shared_ptr &&p): ptr(p.ptr), refcount(p.refcount)
        {
            p.ptr      = nullptr;
            p.refcount = nullptr;
        }

        template<typename U> constexpr shared_ptr(shared_ptr<U> &&p): ptr(p.ptr), refcount(p.refcount)
        {
            p.ptr      = nullptr;
            p.refcount = nullptr;
        }

        shared_ptr(unique_ptr<T> &&p);

        ~shared_ptr()
        {
            _clear();
        }

        shared_ptr &operator=(const shared_ptr &r) noexcept
        {
            if (this == &r)
                return *this;

            _clear();
            this->ptr      = r.ptr;
            this->refcount = r.refcount;

            if (refcount != nullptr) {
                refcount->s.lock();
                refcount->shared_refs += 1;
                refcount->s.unlock();
            }

            return *this;
        }

        template<typename U> shared_ptr<T> &operator=(const shared_ptr<U> &r) noexcept
        {
            if (this == &r)
                return *this;

            _clear();
            this->ptr      = r.ptr;
            this->refcount = r.refcount;

            if (refcount != nullptr) {
                refcount->s.lock();
                refcount->shared_refs += 1;
                refcount->s.unlock();
            }

            return *this;
        }

        shared_ptr &operator=(shared_ptr &&r) noexcept
        {
            if (this == &r)
                return *this;

            _clear();

            this->ptr      = r.ptr;
            this->refcount = r.refcount;

            r.ptr      = nullptr;
            r.refcount = nullptr;

            return *this;
        }

        template<typename U> shared_ptr<T> &operator=(shared_ptr<U> &&r) noexcept
        {
            if (this == &r)
                return *this;

            _clear();

            this->ptr      = r.ptr;
            this->refcount = r.refcount;

            r.ptr      = nullptr;
            r.refcount = nullptr;

            return *this;
        }

        template<typename U> shared_ptr<T> &operator=(unique_ptr<U> &&r)
        {
            unique_ptr<_smart_ptr_refcount_str> new_refcount = nullptr;

            if (r) {
                new_refcount = make_unique<_smart_ptr_refcount_str>(_smart_ptr_refcount_str());
                if (new_refcount)
                    new_refcount->shared_refs++;
                else
                    r = nullptr;
            }

            _clear();

            this->ptr      = r.release();
            this->refcount = new_refcount.release();

            return *this;
        }

        constexpr element_type *get() const noexcept { return ptr; }

        constexpr T &operator*() const noexcept { return *ptr; }

        constexpr T *operator->() const noexcept { return get(); }

        element_type &operator[](unsigned long idx) const { return get()[idx]; }

        long use_count() const noexcept { return refcount ? refcount->shared_refs : 0; }

        bool unique() const noexcept { return use_count() == 1; }

        constexpr explicit operator bool() const { return get() != nullptr; }

        friend class weak_ptr<T>;

        template<class... Args> friend shared_ptr<T> make_shared(Args &&...args);

        template<class A, class U>
        friend shared_ptr<A> dynamic_pointer_cast(const shared_ptr<U> &sp) noexcept;

        template<class A, class U>
        friend shared_ptr<A> static_pointer_cast(const shared_ptr<U> &sp) noexcept;
    };

    template<class T, class U> shared_ptr<T> dynamic_pointer_cast(const shared_ptr<U> &sp) noexcept
    {
        if (sp.ptr == nullptr)
            return nullptr;

        T *ptr = dynamic_cast<T *>(sp.ptr);

        if (ptr == nullptr)
            return nullptr;

        shared_ptr<T> new_ptr;

        new_ptr.ptr      = ptr;
        new_ptr.refcount = sp.refcount;

        if (new_ptr.refcount != nullptr) {
            new_ptr.refcount->s.lock();
            new_ptr.refcount->shared_refs += 1;
            new_ptr.refcount->s.unlock();
        }

        return new_ptr;
    }

    template<class T, class U> shared_ptr<T> static_pointer_cast(const shared_ptr<U> &sp) noexcept
    {
        if (sp.ptr == nullptr)
            return nullptr;

        T *ptr = static_cast<T *>(sp.ptr);

        shared_ptr<T> new_ptr;

        new_ptr.ptr      = ptr;
        new_ptr.refcount = sp.refcount;

        if (new_ptr.refcount != nullptr) {
            new_ptr.refcount->s.lock();
            new_ptr.refcount->shared_refs += 1;
            new_ptr.refcount->s.unlock();
        }

        return new_ptr;
    }

    template<class T, class... Args> shared_ptr<T> make_shared(Args &&...args)
    {
        return shared_ptr<T>(make_unique<T>(forward<Args>(args)...));
    }

    template<class T, class U>
    auto operator==(const shared_ptr<T> &lhs, const shared_ptr<U> &rhs) noexcept
    {
        return lhs.get() == rhs.get();
    }

    template<class T, class U>
    auto operator!=(const shared_ptr<T> &lhs, const shared_ptr<U> &rhs) noexcept
    {
        return lhs.get() != rhs.get();
    }

    template<class T, class U>
    auto operator<(const shared_ptr<T> &lhs, const shared_ptr<U> &rhs) noexcept
    {
        return lhs.get() < rhs.get();
    }

    template<class T> class weak_ptr
    {
        template<typename U> friend class weak_ptr;
        void _clear()
        {
            if (refcount == nullptr)
                return;
            bool delete_refcount = false;

            refcount->s.lock();
            refcount->weak_refs--;
            if (refcount->shared_refs == 0 and refcount->weak_refs == 0) {
                delete_refcount = true;
            }
            refcount->s.unlock();

            if (delete_refcount)
                delete refcount;
        }
    public:
        constexpr weak_ptr() noexcept = default;
        weak_ptr(const weak_ptr<T> &p) noexcept: ptr(p.ptr), refcount(p.refcount)
        {
            if (refcount != nullptr) {
                refcount->s.lock();
                ++refcount->weak_refs;
                refcount->s.unlock();
            }
        }

        weak_ptr(const shared_ptr<T> &p) noexcept: ptr(p.ptr), refcount(p.refcount)
        {
            if (refcount != nullptr) {
                refcount->s.lock();
                ++refcount->weak_refs;
                refcount->s.unlock();
            }
        }

        constexpr weak_ptr(weak_ptr<T> &&r) noexcept: ptr(r.ptr), refcount(r.refcount)
        {
            r.ptr      = nullptr;
            r.refcount = nullptr;
        }

        ~weak_ptr()
        {
            _clear();
        }

        weak_ptr &operator=(const weak_ptr<T> &r) noexcept;

        weak_ptr &operator=(const shared_ptr<T> &r) noexcept
        {
            _clear();
            this->ptr      = r.ptr;
            this->refcount = r.refcount;

            if (refcount != nullptr) {
                refcount->s.lock();
                refcount->weak_refs += 1;
                refcount->s.unlock();
            }

            return *this;
        }

        constexpr weak_ptr &operator=(weak_ptr<T> &&r) noexcept
        {
            _clear();

            ptr      = r.ptr;
            refcount = r.refcount;

            r.ptr      = nullptr;
            r.refcount = nullptr;

            return *this;
        }

        void reset() noexcept;
        void swap(weak_ptr<T> &r) noexcept;

        long use_count() const noexcept
        {
            if (refcount == nullptr)
                return 0;

            return refcount->shared_refs;
        }

        bool expired() const noexcept { return use_count() == 0; }

        shared_ptr<T> lock() const noexcept
        {
            if (refcount == nullptr)
                return shared_ptr<T>();

            shared_ptr<T> p = shared_ptr<T>();

            refcount->s.lock();
            if (refcount->shared_refs) {
                refcount->shared_refs++;
                p.ptr = ptr;
            }
            refcount->s.unlock();

            if (p.ptr != nullptr)
                p.refcount = refcount;
            return p;
        }

        constexpr bool operator<(const weak_ptr &p) const
        {
            return this->ptr == p.ptr ? this->refcount < p.refcount : this->ptr < p.ptr;
        }

        template<class U> bool operator==(const weak_ptr<U> &rhs) noexcept
        {
            return refcount == rhs.refcount;
        }

    private:
        T *ptr                            = nullptr;
        _smart_ptr_refcount_str *refcount = nullptr;
    };

    template<class T> class enable_shared_from_this
    {
    private:
        // TODO: Weak_ptr is reduntant as *this == ptr anyways
        weak_ptr<T> weak_this = weak_ptr<T>();

    protected:
        constexpr enable_shared_from_this() noexcept                            = default;
        enable_shared_from_this(const enable_shared_from_this<T> &obj) noexcept = default;
        enable_shared_from_this(enable_shared_from_this<T> &&obj) noexcept      = delete;
        ~enable_shared_from_this()                                              = default;

        enable_shared_from_this<T> &operator=(const enable_shared_from_this<T> &) noexcept
        {
            return *this;
        }

    public:
        shared_ptr<T> shared_from_this() { return weak_this.lock(); }
        shared_ptr<T const> shared_from_this() const { return weak_this.lock(); }
        weak_ptr<T> weak_from_this() noexcept { return weak_this; }
        weak_ptr<T const> weak_from_this() const noexcept { return weak_this; }

        template<class X> friend class shared_ptr;
    };

    template<class T> shared_ptr<T>::shared_ptr(unique_ptr<T> &&p)
    {
        if (not p.get())
            return;

        unique_ptr<_smart_ptr_refcount_str> refcount_new =
            make_unique<_smart_ptr_refcount_str>(_smart_ptr_refcount_str());
        if (not refcount_new)
            return;

        ptr                   = p.release();
        refcount              = refcount_new.release();
        refcount->shared_refs = 1;

        if constexpr (klib::is_base_of_template<enable_shared_from_this, T>::value) {
            ptr->weak_this = *this;
        }
    }

}

#endif