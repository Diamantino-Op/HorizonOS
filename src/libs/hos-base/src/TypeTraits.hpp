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

    template<typename Base, typename Derived>
struct is_base_of
    : klib::integral_constant<bool, klib::is_class<Base>::value &&
                                        klib::is_class<Derived>::value
                                            &&decltype(klib_is_base_of_details::test_pre_is_base_of<
                                                       Base, Derived>(0))::value> {
    };
}

#endif