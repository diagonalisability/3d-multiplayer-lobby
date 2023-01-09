#pragma once
#include<iostream> // for transput streaming
#include"common.hpp" // for U8L

typedef U8F BitC;
namespace Tag { struct FromInner {} constexpr fromInner; }
template<typename O, BitC partialBitC>
struct FixedPoint {
	static auto constexpr scale= 1<<partialBitC;
	O o;
	constexpr FixedPoint(Tag::FromInner, O);
	template<typename Float>
	constexpr FixedPoint(Float);
};

template<typename O, BitC pbc>
constexpr FixedPoint<O, pbc>::FixedPoint(Tag::FromInner, O const o): o{o} {}

template<typename O, BitC pbc>
template<typename Number>
constexpr FixedPoint<O, pbc>::FixedPoint(Number const n): o{
	static_cast<O>(n * scale)
} {}

template<typename Float, typename O, BitC pbc>
Float constexpr convertToFloat(FixedPoint<O, pbc> const fp) {
	return static_cast<Float>(fp.o) / fp.scale;
}

// arithmetic negation
template<typename O, BitC pbc>
FixedPoint<O, pbc> constexpr operator-(
	FixedPoint<O, pbc> const fp
) {
	return { Tag::fromInner, -fp.o };
}

// addition
template<typename O, BitC pbc>
FixedPoint<O, pbc> constexpr operator+(
	FixedPoint<O, pbc> const fp0,
	FixedPoint<O, pbc> const fp1
) {
	return { Tag::fromInner, fp0.o + fp1.o };
}
template<typename O, BitC pbc, typename Addend>
FixedPoint<O, pbc> constexpr operator+(
	FixedPoint<O, pbc> const fp,
	Addend const x
) {
	return fp + FixedPoint<O, pbc>{x};
}
template<typename O, BitC pbc, typename Addend>
FixedPoint<O, pbc> constexpr operator+(
	Addend const x,
	FixedPoint<O, pbc> const fp
) {
	return fp + x;
}

// subtraction
template<typename O, BitC pbc>
FixedPoint<O, pbc> constexpr operator-(
	FixedPoint<O, pbc> const fp0,
	FixedPoint<O, pbc> const fp1
) {
	return fp0 + -fp1;
}
template<typename O, BitC pbc, typename Subtrahend>
FixedPoint<O, pbc> constexpr operator-(
	FixedPoint<O, pbc> const fp0,
	Subtrahend const x
) {
	return fp0 - FixedPoint<O, pbc>{x};
}
template<typename O, BitC pbc, typename Minuend>
FixedPoint<O, pbc> constexpr operator-(
	Minuend const x,
	FixedPoint<O, pbc> const fp0
) {
	return FixedPoint<O, pbc>{x} - fp0;
}

// additive assignment
template<typename O, BitC pbc>
FixedPoint<O, pbc> &operator+=(FixedPoint<O, pbc> &fp0, FixedPoint<O, pbc> const fp1) {
	fp0.o+= fp1.o;
	return fp0;
}
template<typename O, BitC pbc, typename Addend>
FixedPoint<O, pbc> &operator+=(FixedPoint<O, pbc> &fp, Addend const x) {
	fp+= FixedPoint<O, pbc>{x};
	return fp;
}

// subtractive assignment
template<typename O, BitC pbc>
FixedPoint<O, pbc> &operator-=(FixedPoint<O, pbc> &fp0, FixedPoint<O, pbc> const fp1) {
	fp0.o-= fp1.o;
	return fp0;
}
template<typename O, BitC pbc, typename Subtrahend>
FixedPoint<O, pbc> &operator-=(FixedPoint<O, pbc> &fp, Subtrahend const x) {
	fp-= FixedPoint<O, pbc>{x};
	return fp;
}

// default formatting, for easy debugging
template<typename O, BitC pbc>
std::ostream &operator<<(std::ostream &os, FixedPoint<O, pbc> const fp) {
	auto const precision= 4u;
	auto const existingPrecision= os.precision(precision);
	os << convertToFloat<float>(fp);
	os.precision(existingPrecision);
	return os;
}
