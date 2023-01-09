#pragma once
#include"array.hpp"
#include"common.hpp"
namespace VectorNS {
	template<typename Component, std::size_t componentC0>
	struct Vector : StaticArray<Component, componentC0> {
		typedef StaticArray<Component, componentC0> Base;
		using Base::StaticArray;
		Vector(Vector const&);
		template<std::size_t componentC1, typename= std::enable_if_t<componentC0 == componentC1>>
		Vector(Component const (&)[componentC1]);
		Vector &operator=(Vector const&);
	};

	template<typename C, std::size_t cc>
	Vector<C, cc>::Vector(Vector const &other): Base{
		Tag::constructWithGeneratedArgs,
		[&other](auto const &cons, auto const i) { cons(other[i]); }
	} {}

	template<typename C, std::size_t cc0>
	template<std::size_t cc1, typename>
	Vector<C, cc0>::Vector(C const (&arr)[cc1]):
		Base{Tag::listInitialise<C>, arr}
	{}
	
	template<typename C, std::size_t cc>
	Vector<C, cc> &Vector<C, cc>::operator=(Vector<C, cc> const &other) {
		std::copy(begin(other), end(other), begin(*this));
		return *this;
	}

	template<typename C, std::size_t cc>
	C &getX(Vector<C, cc> &vector) {
		static_assert(2 == cc || 3 == cc);
		return vector[0];
	}
	template<typename C, std::size_t cc>
	C const &getX(Vector<C, cc> const &vector) {
		static_assert(2 == cc || 3 == cc);
		return vector[0];
	}
	template<typename C, std::size_t cc>
	C &getY(Vector<C, cc> &vector) {
		static_assert(2 == cc || 3 == cc);
		return vector[1];
	}
	template<typename C, std::size_t cc>
	C const &getY(Vector<C, cc> const &vector) {
		static_assert(2 == cc || 3 == cc);
		return vector[1];
	}
	template<typename C, std::size_t cc>
	C &getZ(Vector<C, cc> &vector) {
		static_assert(3 == cc);
		return vector[2];
	}
	template<typename C, std::size_t cc>
	C const &getZ(Vector<C, cc> const &vector) {
		static_assert(3 == cc);
		return vector[2];
	}
}

using VectorNS::Vector;
