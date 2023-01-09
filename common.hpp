#pragma once
#include <iostream>
#define WATCH(X) do { std::cout << (#X) << " = " << (X) << "\n"; } while(0)
#define STR_INNER(X) #X
#define STR(X) STR_INNER(X)
#define SOURCE_LOCATION\
		__FILE__\
		":"\
		STR(__LINE__)
#define ASSERT(X) do if(!(X)) {\
	std::cout <<\
		"assertion failed: "\
		#X\
		" evaluated to false (at "\
		SOURCE_LOCATION\
		")\n" << std::flush;\
	std::exit(1);\
} while(0)
#define PERROR_ASSERT(X) do if(!(X)) {\
	std::cout <<\
		"assertion failed: "\
		#X\
		" (at "\
		SOURCE_LOCATION\
		"), error: ";\
	perror(nullptr);\
	std::cout << std::flush;\
	std::exit(1);\
} while(0)
#define UNIMPLEMENTED do {\
	std::cout <<\
		"control reached unimplemented code at "\
		SOURCE_LOCATION\
		"\n";\
	std::exit(1);\
} while(0)
// https://stackoverflow.com/questions/24481810/how-to-remove-the-enclosing-parentheses-with-macro
#define ARGS(...) __VA_ARGS__
#define STRIP_PARENS(X) X
#define NUMBER_TYPE(X) NumberType<decltype(X), X>

typedef std::uint8_t U8;
typedef std::uint_fast8_t U8F;
typedef std::uint_least8_t U8L;
typedef std::int8_t S8;
typedef std::int_fast8_t S8F;
typedef std::int_least8_t S8L;
typedef std::uint16_t U16;
typedef std::uint_fast16_t U16F;
typedef std::uint_least16_t U16L;
typedef std::int16_t S16;
typedef std::int_fast16_t S16F;
typedef std::int_least16_t S16L;
typedef std::uint32_t U32;
typedef std::uint_fast32_t U32F;
typedef std::uint_least32_t U32L;
typedef std::int32_t S32;
typedef std::int_fast32_t S32F;
typedef std::int_least32_t S32L;
typedef std::uint64_t U64;
typedef std::uint_fast64_t U64F;
typedef std::uint_least64_t U64L;
typedef std::int64_t S64;
typedef std::int_fast64_t S64F;
typedef std::int_least64_t S64L;

template<typename I>
struct FastIntegerH;
template<> struct FastIntegerH<U8> { typedef U8F O; };
template<> struct FastIntegerH<S8> { typedef S8F O; };
template<> struct FastIntegerH<U16> { typedef U16F O; };
template<> struct FastIntegerH<S16> { typedef S16F O; };
template<> struct FastIntegerH<U32> { typedef U32F O; };
template<> struct FastIntegerH<S32> { typedef S32F O; };
template<> struct FastIntegerH<U64> { typedef U64F O; };
template<> struct FastIntegerH<S64> { typedef S64F O; };
template<typename I>
using FastInteger= typename FastIntegerH<I>::O;

void error(char const *message);
float constexpr tau= 6.283185307179586;

template<typename T>
T getUninitialised() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
	T x;
	return x;
#pragma GCC diagnostic pop
}
template<typename T, typename F>
T initWithDefaulted(F &&f) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
	T x;
	f(x);
#pragma GCC diagnostic pop
	return x;
}

// "non-type template argument"
// std::integral_constant just with different names
#define MAKE_NTTA(X) NTTA<decltype(X), (X)>
template<typename O_, O_ o_>
struct NTTA {
	typedef O_ O;
	static auto constexpr o= o_;
};

template<typename...>
struct TypeList;

typedef TypeList<U8L, U16L, U32L, U64L, std::size_t> SizeTypes;

template<typename TypeList0, typename TypeList1>
struct ConcatenateH;
template<typename ...Types0, typename ...Types1>
struct ConcatenateH<TypeList<Types0...>, TypeList<Types1...>> {
	typedef TypeList<Types0..., Types1...> O;
};
template<typename TypeList0, typename TypeList1>
using Concatenate= typename ConcatenateH<TypeList0, TypeList1>::O;

template<typename O_>
struct IdentityH {
	typedef O_ O;
};
template<std::size_t, typename...>
struct TightSizeTypeH;
template<std::size_t n, typename Head, typename ...Rest>
struct TightSizeTypeH<n, TypeList<Head, Rest...>> {
	typedef typename std::conditional_t<
		n == static_cast<Head>(n),
		IdentityH<Head>,
		TightSizeTypeH<n, TypeList<Rest...>>
	>::O O;
};
// https://stackoverflow.com/questions/74244055/in-c-get-smallest-integer-type-that-can-hold-given-amount-of-bits
// N must be an instance of NTTA
template<std::size_t n>
using TightSizeType= typename TightSizeTypeH<n, SizeTypes>::O;
template<std::size_t n>
auto constexpr tightenSizeType= static_cast<TightSizeType<n>>(n);

template<typename T, std::size_t len>
auto constexpr length(T const (&a)[len]) {
	return tightenSizeType<len>;
}

template<typename Container, typename El>
bool contains(Container const &c, El const &e) {
	return c.find(e) != c.end();
}

template<typename T>
T const &makeConst(T &x) {
	return x;
}

template<typename T>
T &assertExists(T *const x) {
	ASSERT(x);
	return *x;
}

// https://en.cppreference.com/w/cpp/utility/unreachable
[[noreturn]] inline void unreachable() {
#ifdef __GNUC__ // GCC, Clang, ICC
	__builtin_unreachable();
#elif defined(_MSC_VER) // MSVC
	__assume(false);
#endif
}

// heavy wizardry
// https://stackoverflow.com/questions/281818/unmangling-the-result-of-stdtype-infoname
#include<typeinfo> // for getting type names
#include<cxxabi.h> // for demangling type names
#include<memory> // std::unique_ptr
template<typename A> std::string typeName() {
    char const* name= typeid(A).name();
    signed status;
    std::unique_ptr<char,void(*)(void*)> res {
        abi::__cxa_demangle(name,NULL,NULL,&status),
        std::free
    };
    return reinterpret_cast<char const*>((status==0) ? res.get() : name);
}
