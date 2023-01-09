#pragma once
#include<new> // std::launder
#include<tuple>
#include<utility>
#include<vector>
#include"common.hpp"
#include"scope-guard.hpp"

// empty structs, used to select particular constructors
namespace Tag {
	struct DefaultInitialise {} constexpr defaultInitialise;
	struct ConstructWithUniformArgs {} constexpr constructWithUniformArgs;
	struct ConstructWithGeneratedArgs {} constexpr constructWithGeneratedArgs;
	template<typename El>
	struct ListInitialise {};
	template<typename El>
	ListInitialise<El> constexpr listInitialise;
	struct Empty {} constexpr empty;
	template<typename El>
	struct ElementTypeHint {};
	template<typename El>
	ElementTypeHint<El> constexpr elementTypeHint;
	struct IgnoreTrailingNull {} constexpr ignoreTrailingNull;
}

#define ASSERT_INTEGRAL(TYPE) static_assert(std::is_integral_v<TYPE>)

// https://en.cppreference.com/w/cpp/utility/launder
template<typename El, typename Index, typename Stride= TightSizeType<sizeof(El)>>
El const *getElementPointer(
	char const *const arr,
	Index const i,
	Stride const stride= tightenSizeType<sizeof(El)>
) {
	ASSERT_INTEGRAL(Index);
	ASSERT_INTEGRAL(Stride);
	return std::launder(reinterpret_cast<El const*>(arr + stride*i));
}
template<typename El, typename Index, typename Stride= TightSizeType<sizeof(El)>>
El *getElementPointer(
	char *const arr,
	Index const i,
	Stride const stride= tightenSizeType<sizeof(El)>
) {
	ASSERT_INTEGRAL(Index);
	ASSERT_INTEGRAL(Stride);
	return const_cast<El*>(getElementPointer<El>(
		const_cast<char const*>(arr), i
	));
}

// helper functions for constructing arrays of objects
template<typename El, typename Size, typename Func>
void constructArrayGeneric(char *const arr, Size const size, Func &&construct) {
	ASSERT_INTEGRAL(Size);
	Size i= 0;
	ScopeFailGuard guard{[&]{
		// an exception was thrown,
		// so destroy the elements we did manage to create
		for(; i--;)
			getElementPointer<El>(arr, i)->~El();
	}};
	for(; i < size; ++i)
		construct(i);
}
template<typename El>
struct ConstructObjectWithGeneratedArgs {
	char *pos;
	template<typename ...Args>
	El &operator()(Args &&...args) const {
		return *new(pos) El{std::forward<Args>(args)...};
	}
};
// $generateArgs is passed the current index and a callback
// $generateArgs must call the callback passed to it with the args it wants forwarded to the ctor of El
template<typename El, typename Size, typename Func>
void constructArrayWithGeneratedArgs(char *const arr, Size const size, Func &&generateArgs) {
	ASSERT_INTEGRAL(Size);
	constructArrayGeneric<El>(arr, size, [arr, &generateArgs](auto const i) {
		generateArgs(ConstructObjectWithGeneratedArgs<El>{arr + sizeof(El)*i}, i);
	});
}
// this kind of struct helper is used throughout this file because explicit
// lambda template parameters is a C++20 feature
template<typename El, typename Size>
struct ElementwiseConstructH {
	ASSERT_INTEGRAL(Size);
	char *arr;
	Size i;
	template<typename ...Args>
	void operator()(Args &&...args) {
		new(arr + i*sizeof(El)) El{std::forward<Args>(args)...};
	}
};
template<typename El, typename Size, typename ...Args0>
void constructArrayWithUniformArgs(char *const arr, Size const size, Args0 &&...args) {
	ASSERT_INTEGRAL(Size);
	constructArrayGeneric<El>(arr, size, [&,args = std::forward_as_tuple(args...)] (auto const i) {
		std::apply(ElementwiseConstructH<El, Size>{arr, i}, args);
	});
}
template<typename El, typename Size>
void defaultInitialise(char *const arr, Size const size) {
	ASSERT_INTEGRAL(Size);
	constructArrayGeneric<El>(arr, size, [&](Size const i) {
		new(arr + i*sizeof(El)) El;
	});
}

template<typename T>
using EnableIfIntegral= std::enable_if_t<std::is_integral_v<T>>;

// like std::array but with a bunch of different ways to construct it
// the size type to use for indexing is the fastest type that can hold $size0
template<typename El0, std::size_t size0>
struct StaticArray {
	typedef El0 El;
	static_assert(!std::is_const_v<El>);
	typedef FastInteger<TightSizeType<size0>> FastSize;
	static auto constexpr size= size0;
	alignas(El) char mem[size * sizeof(El)];
	StaticArray(StaticArray&&);
	StaticArray(Tag::DefaultInitialise);
	template<typename F>
	StaticArray(Tag::ConstructWithGeneratedArgs, F&&);
	template<typename... Args>
	StaticArray(Tag::ConstructWithUniformArgs, Args &&...);
	template<typename El1, std::size_t size1, typename= std::enable_if_t<std::is_same_v<El, El1> && size==size1>>
	StaticArray(Tag::ListInitialise<El1>, El1 const (&)[size1]);
	~StaticArray();
	StaticArray &operator=(StaticArray const&);
	template<typename Index, typename= EnableIfIntegral<Index>>
	El &operator[](Index);
	template<typename Index, typename= EnableIfIntegral<Index>>
	El const &operator[](Index) const;
};

template<typename El, std::size_t size>
StaticArray(Tag::ListInitialise<El>, El const (&arr)[size]) -> StaticArray<El, size>;

template<typename El, std::size_t size>
StaticArray<El, size>::StaticArray(StaticArray &&other): StaticArray {
	Tag::constructWithGeneratedArgs,
	[&other](auto const &cons, auto const i) { cons(std::move(other[i])); }
} {}

template<typename El, std::size_t size>
template<typename Func>
StaticArray<El, size>::StaticArray(Tag::ConstructWithGeneratedArgs, Func &&f) {
	constructArrayWithGeneratedArgs<El>(mem, static_cast<FastSize>(size), std::forward<Func>(f));
}
template<typename El, std::size_t size>
StaticArray<El, size>::StaticArray(Tag::DefaultInitialise) {
	defaultInitialise<El, FastSize>(mem, static_cast<FastSize>(size));
}
template<typename El, std::size_t size>
template<typename ...Args>
StaticArray<El, size>::StaticArray(Tag::ConstructWithUniformArgs, Args &&...args) {
	constructArrayWithUniformArgs<El, FastSize>(
		mem,
		static_cast<FastSize>(size),
		std::forward<Args>(args)...
	);
}
template<typename El0, std::size_t size0>
template<typename El1, std::size_t size1, typename>
StaticArray<El0, size0>::StaticArray(Tag::ListInitialise<El1>, El1 const (&arr)[size1]) {
	constructArrayWithGeneratedArgs<El0>(
		mem, static_cast<FastSize>(size0),
		[&arr](auto const &cons, auto const i) {
			cons(arr[i]);
		}
	);
}

template<typename El, std::size_t size>
StaticArray<El, size>::~StaticArray() {
	for(auto &el : *this)
		el.~El();
}

template<typename El, std::size_t size, typename ...Args>
void destroy(StaticArray<El, size> &arr, Args &&...args) {
	for(auto &el : arr)
		destroy(el, std::forward<Args>(args)...);
}

template<typename El, std::size_t size>
El *getData(StaticArray<El, size> &arr) {
	return getElementPointer<El>(arr.mem, 0);
}
template<typename El, std::size_t size>
El const *getData(StaticArray<El, size> const &arr) {
	return getElementPointer<El>(arr.mem, 0);
}

template<typename El, std::size_t size>
template<typename I, typename>
El &StaticArray<El, size>::operator[](I const i) {
	return getData(*this)[i];
}
template<typename El, std::size_t size>
template<typename I, typename>
El const &StaticArray<El, size>::operator[](I const i) const {
	return getData(*this)[i];
}

template<typename El, std::size_t size>
El *begin(StaticArray<El, size> &arr) {
	return getData(arr);
}
template<typename El, std::size_t size>
El const *begin(StaticArray<El, size> const &arr) {
	return getData(arr);
}

template<typename El, std::size_t size>
El *end(StaticArray<El, size> &arr) {
	return getData(arr) + size;
}
template<typename El, std::size_t size>
El const *end(StaticArray<El, size> const &arr) {
	return getData(arr) + size;
}

template<typename El, std::size_t size>
auto getSize(StaticArray<El, size> const &arr) {
	return static_cast<typename decltype(arr)::Size>(size);
}

template<typename El>
struct HeapArray {
	static_assert(!std::is_const_v<El>);
	char *mem= nullptr;
	HeapArray(HeapArray const&)= delete;
	HeapArray(HeapArray&&);
	template<typename Size, typename F>
	HeapArray(Tag::ConstructWithGeneratedArgs, Size, F&&);
	template<typename Size, typename ...Args>
	HeapArray(Tag::ConstructWithUniformArgs, Size, Args&&...);
	template<typename Size>
	HeapArray(Tag::DefaultInitialise, Size);
	~HeapArray();
	template<typename Index, typename= EnableIfIntegral<Index>>
	El &operator[](Index);
	template<typename Index, typename= EnableIfIntegral<Index>>
	El const &operator[](Index) const;
};

template<typename El, typename Size, typename ...Args>
void destroy(
	HeapArray<El> &arr,
	Size size,
	bool const shouldKeepAllocation,
	Args &&...args
) {
	ASSERT_INTEGRAL(Size);
	for(; size; --size) {
		auto &el= arr[size - 1];
		if constexpr(sizeof...(args))
			destroy(el, std::forward<Args>(args)...);
		el.~El();
	}
	if(!shouldKeepAllocation) {
		delete[] arr.mem;
		arr.mem= nullptr;
	}
}

// move ctor
template<typename El>
HeapArray<El>::HeapArray(HeapArray &&other):
	mem{other.mem}
{
	other.mem= nullptr;
}

template<typename Alignment, typename Size, typename Count>
inline char *allocAlignedMemory(Alignment const alignment, Size const size, Count const count) {
	ASSERT_INTEGRAL(Alignment);
	ASSERT_INTEGRAL(Size);
	ASSERT_INTEGRAL(Count);
	return new(std::align_val_t{alignment}) char[count * size];
}
template<typename El, typename Size>
char *allocAlignedMemory(Size const count) {
	return allocAlignedMemory(alignof(El), sizeof(El), count);
}

template<typename El>
template<typename Size, typename Func>
HeapArray<El>::HeapArray(Tag::ConstructWithGeneratedArgs, Size const size, Func &&f):
	mem{allocAlignedMemory<El>(size)}
{
	constructArrayWithGeneratedArgs<El>(mem, size, std::forward<Func>(f));
}
template<typename El>
template<typename Size, typename ...Args>
HeapArray<El>::HeapArray(Tag::ConstructWithUniformArgs, Size const size, Args &&...args):
	mem{allocAlignedMemory<El>(size)}
{
	constructArrayWithUniformArgs<El>(mem, size, std::forward<Args>(args)...);
}
template<typename El>
template<typename Size>
HeapArray<El>::HeapArray(Tag::DefaultInitialise, Size const size):
	mem{allocAlignedMemory<El>(size)}
{
	defaultInitialise<El>(mem, size);
}

// this struct can't destroy itself because it doesn't store the array size,
// which is needed to know how many elements need to have their destructors
// called. SizedHeapArray however, does store its size, so it can properly
// destroy itself, and its destructor does so.
template<typename El>
HeapArray<El>::~HeapArray() {
	if(mem) {
		ASSERT(std::is_trivially_destructible_v<El>);
		delete[] mem;
	}
}

template<typename T>
struct IsTuple: std::false_type {};
template<typename ...Params>
struct IsTuple<std::tuple<Params...>>: std::true_type {};

template<typename El, typename Size>
struct DestroyHeapArrayH {
	HeapArray<El> &arr;
	Size oldSize;
	template<typename ...DestroyArgs>
	void operator()(DestroyArgs &&...destroyArgs) {
		destroy(arr, oldSize, true, std::forward<DestroyArgs>(destroyArgs)...);
	}
};
template<typename El, typename Size, typename DestroyArgsTuple>
void destroyH(HeapArray<El> &arr, Size const oldSize, DestroyArgsTuple &&destroyArgs) {
	ASSERT_INTEGRAL(Size);
	std::apply(
		DestroyHeapArrayH<El, Size>{arr, oldSize},
		std::forward<DestroyArgsTuple>(destroyArgs)
	);
}

template<bool wasDestroyed, typename El, typename Size>
void recreateHelper(HeapArray<El> &arr, Size const newSize) {
	ASSERT_INTEGRAL(Size);
	static_assert(wasDestroyed || std::is_trivially_destructible_v<El>);
	delete[] arr.mem;
	arr.mem= new(std::align_val_t{alignof(El)}) char[newSize * sizeof(El)];
}
template<
	typename El,
	typename OldSize,
	typename NewSize,
	typename Func,
	typename DestroyArgsTuple,
	typename=std::enable_if_t<IsTuple<std::remove_cv_t<DestroyArgsTuple>>::value>
> void destroyAndRecreateByCallingWithIndex(
	HeapArray<El> &arr,
	OldSize const oldSize,
	DestroyArgsTuple &&destroyArgs,
	NewSize const newSize,
	Func &&f
) {
	destroyH(arr, oldSize, std::forward<DestroyArgsTuple>(destroyArgs));
	if (oldSize != newSize) {
		delete[] arr.mem;
		arr.mem= new(std::align_val_t{alignof(El)}) char[newSize * sizeof(El)];
	}
	constructArrayWithGeneratedArgs<El>(arr.mem, newSize, std::forward<Func>(f));
}
template<
	bool wasDestroyed=false,
	typename El,
	typename OldSize,
	typename NewSize,
	typename ...Args
> void recreateElementwise(
	HeapArray<El> &arr,
	OldSize const oldSize,
	NewSize const newSize,
	Args &&...args
) {
	ASSERT_INTEGRAL(OldSize);
	ASSERT_INTEGRAL(NewSize);
	std::cout << "recreateElementwise called\n";
	std::cout << "recreateElementwise, arr.mem = " << static_cast<void*>(arr.mem) << "\n";
	if (oldSize != newSize)
		recreateHelper<wasDestroyed>(arr, newSize);
	constructArrayWithUniformArgs<El>(arr.mem, newSize, std::forward<Args>(args)...);
}
template<typename El, typename OldSize, typename NewSize>
struct DestroyAndRecreateElementwiseH {
	ASSERT_INTEGRAL(OldSize);
	ASSERT_INTEGRAL(NewSize);
	HeapArray<El> &arr;
	OldSize oldSize;
	NewSize newSize;
	template<typename ...CreateArgs>
	void operator()(CreateArgs &&...createArgs) {
		std::cout << "destroyAndRecreateElementwiseH::operator() called\n";
		recreateElementwise<true>(arr, oldSize, newSize, std::forward<CreateArgs>(createArgs)...);
	}
};
template<
	typename El,
	typename OldSize,
	typename NewSize,
	typename DestroyArgsTuple,
	typename CreateArgsTuple
>
void destroyAndRecreateElementwise(
	HeapArray<El> &arr,
	OldSize const oldSize,
	DestroyArgsTuple &&destroyArgsTuple,
	NewSize const newSize,
	CreateArgsTuple &&createArgsTuple
) {
	ASSERT_INTEGRAL(OldSize);
	ASSERT_INTEGRAL(NewSize);
	std::cout << "destroyAndRecreateElementwise called\n";
	destroyH(arr, oldSize, std::forward<DestroyArgsTuple>(destroyArgsTuple));
	std::apply(
		DestroyAndRecreateElementwiseH<El, OldSize, NewSize>{arr, oldSize, newSize},
		std::forward<CreateArgsTuple>(createArgsTuple)
	);
}

template<typename El, typename Size>
void recreateDefault(HeapArray<El> &arr, Size const newSize) {
	ASSERT_INTEGRAL(Size);
	recreateHelper<false>(arr, newSize);
	defaultInitialise<El>(arr.mem, newSize);
}

template<typename El>
template<typename I, typename>
El const &HeapArray<El>::operator[](I const i) const {
	return *getElementPointer<El>(mem, i);
}

template<typename El>
template<typename I, typename>
El &HeapArray<El>::operator[](I const i) {
	return *getElementPointer<El>(mem, i);
}

template<typename El>
El *getData(HeapArray<El> &arr) {
	return getElementPointer<El>(arr.mem, 0);
}

template<typename El>
El const *getData(HeapArray<El> const &arr) {
	return getElementPointer<El>(arr.mem, 0);
}

template<typename El, typename OldSize, typename NewSize, typename ...CreateArgs>
void resizeLarger(
	HeapArray<El> &arr,
	OldSize const oldSize,
	NewSize const newSize,
	CreateArgs &&...createArgs
) {
	ASSERT_INTEGRAL(OldSize);
	ASSERT_INTEGRAL(NewSize);
	// exception handling isn't implemented because it's hard to pick something
	// sane to do in the case that an exception arises, and i don't throw any
	// exceptions
	static_assert(noexcept(El{std::declval<El>()}));
	static_assert(noexcept(El{std::forward<CreateArgs>(createArgs)...}));
	ASSERT(oldSize <= newSize);
	auto *const newArr= new(std::align_val_t{alignof(El)}) char[newSize * sizeof(El)];
	NewSize i= 0;
	for(; i<oldSize; ++i) {
		new(newArr + i*sizeof(El)) El{std::move(arr[i])};
		arr[i].~El();
	}
	delete[] arr.mem;
	for(; i<newSize; ++i)
		new(newArr + i*sizeof(El)) El{std::forward<CreateArgs>(createArgs)...};
	arr.mem= newArr;
}

template<typename El, typename OldSize, typename NewSize, typename ...DestroyArgs>
void resizeSmaller(
	HeapArray<El> &arr,
	OldSize const oldSize,
	NewSize const newSize,
	DestroyArgs &&...destroyArgs
) {
	ASSERT_INTEGRAL(OldSize);
	ASSERT_INTEGRAL(NewSize);
	static_assert(noexcept(El{std::declval<El>()}));
	static_assert(noexcept(destroy(std::declval<El>(), std::forward<DestroyArgs>(destroyArgs)...)));
	ASSERT(newSize <= oldSize);
	auto *const newArr= new(std::align_val_t{alignof(El)}) char[newSize * sizeof(El)];
	OldSize i= 0;
	for(; i<newSize; ++i) {
		new(newArr + i*sizeof(El)) El{std::move(arr[i])};
		arr[i].~El();
	}
	delete[] arr.mem;
	for(; i<oldSize; ++i)
		destroy(arr[i], std::forward<DestroyArgs>(destroyArgs)...);
	arr.mem= newArr;
}

template<typename El, typename Size>
struct SizedArray {
	static_assert(!std::is_const_v<El>);
	ASSERT_INTEGRAL(Size);
	Size size= 0;
	HeapArray<El> o{};
	SizedArray(SizedArray const&)= delete;
	SizedArray(SizedArray&&);
	template<typename F>
	SizedArray(Tag::ConstructWithGeneratedArgs, Size size, F&&, Tag::ElementTypeHint<El> = {});
	template<typename ...Args>
	SizedArray(Tag::ConstructWithUniformArgs, Size size, Args&&..., Tag::ElementTypeHint<El> = {});
	SizedArray(Tag::DefaultInitialise, Size size, Tag::ElementTypeHint<El> = {});
	~SizedArray();
	SizedArray &operator=(SizedArray&&);
	template<typename Index, typename= EnableIfIntegral<Index>>
	El &operator[](Index );
	template<typename Index, typename= EnableIfIntegral<Index>>
	El const &operator[](Index i) const;
};

// move ctor
template<typename El, typename Size>
SizedArray<El, Size>::SizedArray(SizedArray &&other):
	size{other.size},
	o{std::move(other.o)}
{
	other.size= 0;
}

template<typename El, typename Size>
template<typename Func>
SizedArray<El, Size>::SizedArray(
	Tag::ConstructWithGeneratedArgs,
	Size const size,
	Func &&f,
	Tag::ElementTypeHint<El>
):
	size{size},
	o{Tag::constructWithGeneratedArgs, size, std::forward<Func>(f)}
{}
template<typename El, typename Size>
template<typename ...Args>
SizedArray<El, Size>::SizedArray(
	Tag::ConstructWithUniformArgs,
	Size const size,
	Args &&...args,
	Tag::ElementTypeHint<El>
):
	size{size},
	o{Tag::constructWithUniformArgs, size, std::forward<Args>(args)...}
{}
template<typename El, typename Size>
SizedArray<El, Size>::SizedArray(
	Tag::DefaultInitialise,
	Size size,
	Tag::ElementTypeHint<El>
):
	size{size},
	o{Tag::defaultInitialise, size}
{}

template<typename El, typename Size, typename ...DestroyArgs>
void destroy(SizedArray<El, Size> &arr, DestroyArgs &&...destroyArgs) {
	destroy(arr.o, arr.size, false, std::forward<DestroyArgs>(destroyArgs)...);
}

template<typename El, typename Size>
SizedArray<El, Size>::~SizedArray() {
	destroy(*this);
}

template<typename El, typename Size>
SizedArray<El, Size> &SizedArray<El, Size>::operator=(SizedArray &&other) {
	size= other.size;
	o= std::move(other.o);
	other.size= 0;
	return *this;
}

template<typename El, typename Size>
template<typename I, typename>
El &SizedArray<El, Size>::operator[](I const i) {
	return o[i];
}

template<typename El, typename Size>
template<typename I, typename>
El const &SizedArray<El, Size>::operator[](I const i) const {
	return o[i];
}

template<typename El, typename Size>
El *begin(SizedArray<El, Size> &arr) {
	return getData(arr.o);
}

template<typename El, typename Size>
El const *begin(SizedArray<El, Size> const &arr) {
	return getData(arr.o);
}

template<typename El, typename Size>
El *end(SizedArray<El, Size> &arr) {
	return getData(arr.o) + arr.size;
}

template<typename El, typename Size>
El const *end(SizedArray<El, Size> const &arr) {
	return getData(arr.o) + arr.size;
}

template<typename El, typename Size>
void create(SizedArray<El, Size> &arr, Size const size) {
	arr.size= size;
	create(arr.o, size);
}

template<typename El, typename Size>
El *getData(SizedArray<El, Size> &arr) {
	return getData(arr.o);
}
template<typename El, typename Size>
El const *getData(SizedArray<El, Size> const &arr) {
	return getData(arr.o);
}

// "growable" means ammortised constant-time append
template<typename El, typename Size>
struct GrowableArray {
	static_assert(!std::is_const_v<El>);
	static_assert(std::is_trivially_copyable_v<El>);
	ASSERT_INTEGRAL(Size);
	Size size;
	Size capacity;
	char *mem= nullptr;
	GrowableArray(GrowableArray const&)= delete;
	GrowableArray(GrowableArray&&);
	GrowableArray(Tag::DefaultInitialise, Size size, Size initialCap);
	~GrowableArray();
	GrowableArray &operator=(GrowableArray&&);
};

template<typename El, typename Size>
GrowableArray<El, Size>::GrowableArray(GrowableArray &&other):
	size{other.size},
	capacity{other.capacity},
	mem{std::move(other.mem)}
{}

template<typename El, typename Size>
GrowableArray<El, Size>::GrowableArray(Tag::DefaultInitialise, Size const size, Size const initialCap):
	size{size},
	capacity{initialCap},
	mem{allocAlignedMemory<El>(capacity)}
{
	defaultInitialise<El>(mem, size);
}

template<typename Size>
struct GrownArray {
	char *newMemory;
	Size newCapacity;
};
template<typename El, typename Size>
GrownArray<Size> growArray(char *const arr, Size const currentSize, Size const stride=sizeof(El)) {
	// not designed to handle El's ctor throwing an exception
	// apparently std::vector has a guarantee for when the copy ctor throws, but i don't need that
	static_assert(std::is_nothrow_constructible_v<El, El&&>);
	double constexpr growthMultiplier= 1.6f;
	auto const newCap= static_cast<Size>(growthMultiplier * currentSize);
	auto *const newMem= allocAlignedMemory<El>(newCap);
	for(U32 i=0; i<currentSize; ++i) {
		El &old= *getElementPointer<El>(arr, i, stride);
		new(newMem + sizeof(El)*i) El{std::move(old)};
		old.~El();
	}
	delete[] arr;
	return { newMem, newCap };
}

template<typename El, typename Size, typename ...CreateArgs>
void createBack(GrowableArray<El, Size> &arr, CreateArgs &&...createArgs) {
	if(arr.capacity == arr.size) {
		auto const grownArray= growArray<El, Size>(arr.mem, arr.size);
		arr.mem= grownArray.newMem;
		arr.capacity= grownArray.newCap;
	}
	new(arr.mem + sizeof(El)*arr.size) El{std::forward<CreateArgs>(createArgs)...};
	++arr.size;
}

template<bool shouldMakeConst, typename T>
struct ConditionallyMakeConstH {
	typedef T O;
};
template<typename T>
struct ConditionallyMakeConstH<true, T> {
	typedef T const O;
};
template<bool shouldMakeConst, typename T>
using ConditionallyMakeConst= typename ConditionallyMakeConstH<shouldMakeConst, T>::O;

// constness of the array accessible through an object of an instance of this
// template is determined solely by $isConst, and not by the constness of the
// ArrayView
template<typename El_, bool isConst, typename Size0>
struct ArrayView {
	typedef El_ El;
	static_assert(!std::is_const_v<El>);
	ASSERT_INTEGRAL(Size0);
	typedef ConditionallyMakeConst<isConst, El> ElWithConstness;
	ElWithConstness *o;
	Size0 size;
	template<typename Size1>
	ArrayView(ElWithConstness *o, Size1 size);
	template<std::size_t size, bool isConst_= isConst, typename= std::enable_if_t<!isConst_>>
	ArrayView(El (&)[size]);
	template<std::size_t size, bool isConst_= isConst, typename= std::enable_if_t<isConst_>>
	ArrayView(El const (&arr)[size]);
	template<std::size_t size, bool isConst_= isConst, typename= std::enable_if_t<isConst_>>
	ArrayView(Tag::IgnoreTrailingNull, char const (&)[size]);
	template<std::size_t size, bool isConst_= isConst, typename= std::enable_if_t<!isConst_>>
	ArrayView(StaticArray<El, size>&);
	template<std::size_t size, bool isConst_= isConst, typename= std::enable_if_t<isConst_>>
	ArrayView(StaticArray<El, size> const&);
	template<bool isConst_= isConst, typename= std::enable_if_t<!isConst_>>
	ArrayView(std::vector<El>&);
	template<bool isConst_= isConst, typename= std::enable_if_t<isConst_>>
	ArrayView(std::vector<El> const&);
	template<typename Traits, typename Allocator, bool isConst_= isConst, typename= std::enable_if_t<!isConst_>>
	ArrayView(std::basic_string<El, Traits, Allocator>&);
	template<typename Traits, typename Allocator, bool isConst_= isConst, typename= std::enable_if_t<isConst_>>
	ArrayView(std::basic_string<El, Traits, Allocator> const&);
	template<typename Index, typename= std::enable_if_t<!isConst && std::is_integral_v<Index>>>
	El &operator[](Index) const;
	template<typename Index, typename= std::enable_if_t<isConst && std::is_integral_v<Index>>>
	El const &operator[](Index) const;
};

template<typename Size>
using StringView= ArrayView<char, true, Size>;

template<typename El, typename Size>
ArrayView(El *o, Size size)-> ArrayView<std::remove_const_t<El>, std::is_const_v<El>, Size>;
template<typename El, std::size_t size>
ArrayView(El(&)[size]) -> ArrayView<El, false, TightSizeType<size>>;
template<typename El, std::size_t size>
ArrayView(El const(&)[size]) -> ArrayView<El, true, TightSizeType<size>>;
template<std::size_t size>
ArrayView(Tag::IgnoreTrailingNull, char const(&)[size]) ->
	ArrayView<char, true, TightSizeType<size>>;
template<typename El, std::size_t size>
ArrayView(StaticArray<El, size>&) -> ArrayView<El, false, TightSizeType<size>>;
template<typename El, std::size_t size>
ArrayView(StaticArray<El, size> const&) -> ArrayView<El, true, TightSizeType<size>>;
template<typename El, typename Allocator>
ArrayView(std::vector<El, Allocator>&) ->
	ArrayView<El, false, typename std::vector<El, Allocator>::size_type>;
template<typename El, typename Allocator>
ArrayView(std::vector<El, Allocator> const&) ->
	ArrayView<El, true, typename std::vector<El, Allocator>::size_type>;
template<typename El, typename Traits, typename Allocator>
ArrayView(std::basic_string<El, Traits, Allocator>&) ->
	ArrayView<El, false, typename std::basic_string<El, Traits, Allocator>::size_type>;
template<typename El, typename Traits, typename Allocator>
ArrayView(std::basic_string<El, Traits, Allocator> const&) ->
	ArrayView<El, true, typename std::basic_string<El, Traits, Allocator>::size_type>;

template<typename El, bool isConst, typename Size0>
template<typename Size1>
ArrayView<El, isConst, Size0>::ArrayView(ElWithConstness *const o, Size1 const size):
	o{o}, size{size}
{}
template<typename El, bool isConst, typename Size>
template<std::size_t size_, bool, typename>
ArrayView<El, isConst, Size>::ArrayView(El (&arr)[size_]):
	o{arr},
	size{size_}
{}
template<typename El, bool isConst, typename Size>
template<std::size_t size_, bool, typename>
ArrayView<El, isConst, Size>::ArrayView(El const (&arr)[size_]):
	o{arr},
	size{size_}
{
	static_assert(isConst);
}

template<typename El, bool isConst, typename Size>
template<std::size_t size_, bool, typename>
ArrayView<El, isConst, Size>::ArrayView(Tag::IgnoreTrailingNull, char const (&str)[size_]):
	o{str},
	size{size_-1}
{}

template<typename El, bool isConst, typename Size>
template<std::size_t size_, bool, typename>
ArrayView<El, isConst, Size>::ArrayView(StaticArray<El, size_> &arr):
	o{getData(arr)},
	size{static_cast<Size>(size)}
{}
template<typename El, bool isConst, typename Size>
template<std::size_t size_, bool, typename>
ArrayView<El, isConst, Size>::ArrayView(StaticArray<El, size_> const &arr):
	o{getData(arr)},
	size{static_cast<Size>(size_)}
{
	static_assert(isConst);
}

template<typename El, bool isConst, typename Size>
template<bool, typename>
ArrayView<El, isConst, Size>::ArrayView(std::vector<El> &vec):
	o{vec.data()},
	size{vec.size()}
{}
template<typename El, bool isConst, typename Size>
template<bool, typename>
ArrayView<El, isConst, Size>::ArrayView(std::vector<El> const &vec):
	o{vec.data()},
	size{vec.size()}
{
	static_assert(isConst);
}

template<typename El, bool isConst, typename Size>
template<typename Traits, typename Allocator, bool, typename>
ArrayView<El, isConst, Size>::ArrayView(std::basic_string<El, Traits, Allocator> &str):
	o{str.c_str()},
	size{str.size()}
{}
template<typename El, bool isConst, typename Size>
template<typename Traits, typename Allocator, bool, typename>
ArrayView<El, isConst, Size>::ArrayView(std::basic_string<El, Traits, Allocator> const &str):
	o{str.c_str()},
	size{str.size()}
{
	static_assert(isConst);
}

template<typename El, bool isConst, typename Size>
template<typename I, typename>
El &ArrayView<El, isConst, Size>::operator[](I const i) const {
	return o;
}
template<typename El, bool isConst, typename Size>
template<typename I, typename>
El const &ArrayView<El, isConst, Size>::operator[](I const i) const {
	return o;
}

template<typename El, bool isConst, typename Size>
ConditionallyMakeConst<isConst, El> *getData(ArrayView<El, isConst, Size> const &av) {
	return av.o;
}
template<typename El, bool isConst, typename Size>
ConditionallyMakeConst<isConst, El> *begin(ArrayView<El, isConst, Size> const &av) {
	return getData(av);
}
template<typename El, bool isConst, typename Size>
ConditionallyMakeConst<isConst, El> *end(ArrayView<El, isConst, Size> const &av) {
	return getData(av) + av.size;
}

#define STRING_VIEW(CHAR_ARRAY) ArrayView{Tag::ignoreTrailingNull, (CHAR_ARRAY)}

// used by the server because it isn't important which holes in particular get allocated
template<typename El, typename Size_>
struct HoleyArray {
	static_assert(!std::is_const_v<El>);
	typedef Size_ Size;
	typedef FastInteger<Size> FastSize;
	static Size constexpr bucketSize= std::max(sizeof(Size), sizeof(El));
	char *mem{nullptr};
	Size capacity;
	// sorted array of indices of uninitialised elements
	std::vector<Size> holeIs;
	HoleyArray(HoleyArray const&)= delete;
	HoleyArray(Size const initialCap);
	~HoleyArray();
	template<typename Index, typename= EnableIfIntegral<Index>>
	El &operator[](Index);
	template<typename Index, typename= EnableIfIntegral<Index>>
	El const &operator[](Index) const;
};

template<typename El, typename Size>
HoleyArray<El, Size>::HoleyArray(Size const initialCap):
	mem{allocAlignedMemory(
		std::max(alignof(Size), alignof(El)),
		bucketSize,
		initialCap
	)},
	capacity{initialCap}
{
	holeIs.reserve(initialCap);
	for(Size i=0; i<initialCap; ++i)
		holeIs.push_back(i);
}

template<typename El, typename Size, typename Index>
void destroy(HoleyArray<El, Size> &arr, Index const i) {
	arr[i].~El();
	arr.holeIs.insert(
		// this function does a binary search
		std::lower_bound(begin(arr.holeIs), end(arr.holeIs), i), i
	);
}

// passes the index at which the element will be created to $create
// $create should call the callback passed to it with the arguments the caller wants forwarded to El's ctor
// returns whatever $create returns
template<typename El, typename Size, typename CreateFunc>
decltype(auto) emplace(HoleyArray<El, Size> &arr, CreateFunc &&create) {
	if(arr.holeIs.empty()) {
		auto const grownArray= growArray<El>(arr.mem, arr.capacity, arr.bucketSize);
		for(FastInteger<Size> i=arr.capacity; i<grownArray.newCapacity; ++i)
			arr.holeIs.push_back(i);
		arr.mem= grownArray.newMemory;
		arr.capacity= grownArray.newCapacity;
	}
	auto const holeI= arr.holeIs.back();
	arr.holeIs.pop_back();
	return create(
		ConstructObjectWithGeneratedArgs<El>{arr.mem + arr.bucketSize*holeI},
		holeI
	);
}

template<typename El, typename Size>
template<typename I, typename>
El &HoleyArray<El, Size>::operator[](I const i) {
	return *getElementPointer<El>(mem, i, bucketSize);
}
template<typename El, typename Size>
template<typename I, typename>
El const &HoleyArray<El, Size>::operator[](I const i) const {
	return *getElementPointer<El>(mem, i, bucketSize);
}

// calls f with (filledI, oI, element)
template<typename HoleyArray, typename F>
void holeyArrayForeachImpl(HoleyArray &arr, F &&f) {
	typedef typename HoleyArray::FastSize Size;
	Size oI= 0;
	Size filledI= 0;
	for(Size holeII=0; holeII<arr.holeIs.size(); ++oI) {
		if(oI == arr.capacity)
			return;
		if(arr.holeIs[holeII] == oI) {
			++holeII;
			continue;
		}
		f(filledI++, oI, arr[oI]);
	}
	// no more holes
	for(; oI < arr.capacity; ++oI)
		f(filledI++, oI, arr[oI]);
}

template<typename El, typename Size, typename F>
void foreach(HoleyArray<El, Size> &arr, F &&f) {
	holeyArrayForeachImpl(arr, f);
}
template<typename El, typename Size, typename F>
void foreach(HoleyArray<El, Size> const &arr, F &&f) {
	holeyArrayForeachImpl(arr, f);
}

template<typename El, typename Size>
HoleyArray<El, Size>::~HoleyArray() {
	foreach(*this, [](Size, Size, El &el) {
		el.~El();
	});
	delete[] mem;
}

template<typename El, typename Size>
Size size(HoleyArray<El, Size> const &arr) {
	return arr.capacity - arr.holeIs.size();
}

// used by the client because array indices (player ids) need to match what the server says
template<typename El, typename Size>
struct ReplicaHoleyArray {
	char *mem;
	std::vector<Size> filledIs;
	Size size;
	ReplicaHoleyArray()= delete;
	ReplicaHoleyArray(Tag::Empty);
	~ReplicaHoleyArray();
	template<typename Index, typename= EnableIfIntegral<Index>>
	El &operator[](Index);
	template<typename Index, typename= EnableIfIntegral<Index>>
	El const &operator[](Index i) const;
};

template<typename El, typename Size>
ReplicaHoleyArray<El, Size>::ReplicaHoleyArray(Tag::Empty):
	mem{nullptr}
{}

template<typename El, typename Size>
void allocate(ReplicaHoleyArray<El, Size> &arr, Size const size) {
	ASSERT(!arr.mem);
	ASSERT(arr.filledIs.empty());
	arr.mem= allocAlignedMemory<El>(size);
	arr.size= size;
}

template<typename El, typename Size>
template<typename I, typename>
El &ReplicaHoleyArray<El, Size>::operator[](I const i) {
	return *getElementPointer<El>(mem, i);
}
template<typename El, typename Size>
template<typename I, typename>
El const &ReplicaHoleyArray<El, Size>::operator[](I const i) const {
	return *getElementPointer<El>(mem, i);
}

template<typename El, typename Size, typename Func>
void foreach(ReplicaHoleyArray<El, Size> &arr, Func &&func) {
	Size filledI= 0;
	for(Size const i: arr.filledIs)
		func(filledI++, i, *getElementPointer<El>(arr.mem, i));
}

template<typename El, typename Size>
ReplicaHoleyArray<El, Size>::~ReplicaHoleyArray() {
	foreach(*this, [](Size, Size, auto const &el) {
		el.~El();
	});
}

template<typename El, typename Size, typename Index, typename ...CreateArgs>
void emplace(
	ReplicaHoleyArray<El, Size> &arr,
	Index const i,
	CreateArgs &&...createArgs
) {
	new(arr.mem + i*sizeof(El)) El{std::forward<CreateArgs>(createArgs)...};
	arr.filledIs.insert(std::lower_bound(begin(arr.filledIs), end(arr.filledIs), i), i);
}
template<typename El, typename Size, typename Index>
void destroy(ReplicaHoleyArray<El, Size> &arr, Index const i) {
	arr[i].~El();
	auto const it= std::lower_bound(begin(arr.filledIs), end(arr.filledIs), i);
	ASSERT(*it == i);
	arr.filledIs.erase(it);
}

template<typename El, typename Size>
Size size(ReplicaHoleyArray<El, Size> const &arr) {
	return arr.filledIs.size();
}
