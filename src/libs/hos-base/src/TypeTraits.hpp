#ifndef LIB_HOS_TYPETRAITS_HPP
#define LIB_HOS_TYPETRAITS_HPP

#include "Types.hpp"

namespace lib::hos::base {
    template<class T> struct RemoveReference {
        typedef T type;
    };

    template<class T> struct RemoveReference<T &> {
        typedef T type;
    };

    template<class T> struct RemoveReference<T &&> {
        typedef T type;
    };

    template<class T> using removeReferenceT = typename RemoveReference<T>::type;

    template<class T> struct RemoveExtent {
        using type = T;
    };

    template<class T> struct RemoveExtent<T[]> {
        using type = T;
    };

    template<class T, usize N> struct RemoveExtent<T[N]> {
        using type = T;
    };

    template<class T, T v> struct IntegralConstant {
        static constexpr T value = v;
        using valueType = T;
        using type = IntegralConstant;

        constexpr operator valueType() const noexcept {
			return value;
		}

        constexpr valueType operator()() const noexcept {
            return value;
        }
    };

    typedef IntegralConstant<bool, true> trueType;
    typedef IntegralConstant<bool, false> falseType;

    template<class T> struct RemoveCv {
        typedef T type;
    };

    template<class T> struct RemoveCv<const T> {
        typedef T type;
    };

    template<class T> struct RemoveCv<volatile T> {
        typedef T type;
    };

    template<class T> struct RemoveCv<const volatile T> {
        typedef T type;
    };

    template<class T> struct RemoveConst {
        typedef T type;
    };

    template<class T> struct RemoveConst<const T> {
        typedef T type;
    };

    template<class T> struct RemoveVolatile {
        typedef T type;
    };

    template<class T> struct RemoveVolatile<volatile T> {
        typedef T type;
    };

    template<bool B, class T, class F> struct Conditional {
        using type = T;
    };

    template<class T, class F> struct Conditional<false, T, F> {
        using type = F;
    };

    template<bool B, class T, class F> using conditionalT = typename Conditional<B, T, F>::type;

    template<class B1, class... Bn> struct Conjunction<B1, Bn...>: conditionalT<bool(B1::value), Conjunction<Bn...>, B1> {};

    template<class...> struct Conjunction: trueType {};

    template<class B1> struct Conjunction<B1>: B1 {};

    template<class T> struct IsLvalueReference: falseType {};

    template<class T> struct IsLvalueReference<T &>: trueType {};

    template<class T> constexpr bool isLvalueReferenceV = IsLvalueReference<T>::value;

    template<class T, class U> struct IsSame: falseType {};

    template<class T> struct IsSame<T, T>: trueType {};

    template<class T> struct IsVoid: IsSame<void, typename RemoveCv<T>::type> {};

    template<typename T> struct IsUnion: public IntegralConstant<bool, __is_union(T)> {};

    template<class T> IntegralConstant<bool, !IsUnion<T>::value> Test(int T::*);

    template<class> falseType Test(...);

    template<class T> struct IsClass: decltype(Test<T>(nullptr)) {};

    template<typename B> trueType TestPrePtrConvertible(const volatile B *);
    template<typename> falseType TestPrePtrConvertible(const volatile void *);

    template<typename, typename> auto TestPreIsBaseOf(...) -> trueType;
    template<typename B, typename D> auto TestPreIsBaseOf(int) -> decltype(TestPrePtrConvertible<B>(static_cast<D *>(nullptr)));

    template<typename Base, typename Derived> struct IsBaseOf : IntegralConstant<bool, IsClass<Base>::value && IsClass<Derived>::value && decltype(TestPreIsBaseOf<Base, Derived>(0))::value> {};

    template<class T> struct TypeIdentity {
        using type = T;
    };

    template<class T> auto TryAddLvalueReference(int) -> TypeIdentity<T &>;
    template<class T> auto TryAddLvalueReference(...) -> TypeIdentity<T>;

    template<class T> auto TryAddRvalueReference(int) -> TypeIdentity<T &&>;
    template<class T> auto TryAddRvalueReference(...) -> TypeIdentity<T>;

    template<class T> struct AddLvalueReference: decltype(TryAddLvalueReference<T>(0)) {};

    template<class T> struct AddRvalueReference: decltype(TryAddRvalueReference<T>(0)) {};

    template<typename T> constexpr bool alwaysFalse = false;

    template<typename T> typename AddRvalueReference<T>::type DeclVal() noexcept {
        static_assert(alwaysFalse<T>, "DeclVal not allowed in an evaluated context");
    }

    template<class T> auto TestReturnable(int) -> decltype(void(static_cast<T (*)()>(nullptr)), trueType {});
    template<class> auto TestReturnable(...) -> falseType;

    template<class From, class To> auto TestImplicitlyConvertible(int) -> decltype(void(DeclVal<void (&)(To)>()(DeclVal<From>())), trueType {});
    template<class, class> auto TestImplicitlyConvertible(...) -> falseType;

    template<class From, class To> struct IsConvertible : IntegralConstant<
          bool,
          (decltype(TestReturnable<To>(0))::value &&
           decltype(TestImplicitlyConvertible<From, To>(0))::value) ||
              (IsVoid<From>::value && IsVoid<To>::value)> {
    };

    template<class From, class To> struct IsNoThrowConvertible: Conjunction<IsVoid<From>, IsVoid<To>> {};

    template<class From, class To>
        requires requires {
            static_cast<To (*)()>(nullptr);
            {
                DeclVal<void (&)(To) noexcept>()(DeclVal<From>())
            } noexcept;
        }
    struct IsNoThrowConvertible<From, To>: trueType {};

    template<template<typename...> class base, typename derived> struct IsBaseOfTemplateImpl {
        template<typename... Ts> static constexpr trueType Test(const base<Ts...> *);
        static constexpr falseType Test(...);
        using type = decltype(Test(DeclVal<derived *>()));
    };

    template<template<typename...> class base, typename derived> using isBaseOfTemplate = typename IsBaseOfTemplateImpl<base, derived>::type;
}

#endif