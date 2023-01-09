#pragma once
#include<exception>
#include"common.hpp"

template<typename F>
struct ScopeExitGuard {
	F f;
	ScopeExitGuard(F &&f);
	~ScopeExitGuard();
};

template<typename F>
ScopeExitGuard<F>::ScopeExitGuard(F &&f): f{f} {}

template<typename F>
ScopeExitGuard<F>::~ScopeExitGuard() {
	f();
}

template<typename F>
struct ScopeFailGuard {
	F f;
	unsigned inFlightExceptionCWhenCreated;
	ScopeFailGuard(F &&f);
	~ScopeFailGuard();
};

template<typename F>
ScopeFailGuard<F>::ScopeFailGuard(F &&f):
	f{std::forward<F>(f)},
	inFlightExceptionCWhenCreated{[]{
		signed const uncaughtExceptionC = std::uncaught_exceptions();
		// i'm not sure why this value would be negative, but it's signed,
		// so why not make sure
		ASSERT(0 <= uncaughtExceptionC);
		return static_cast<unsigned>(uncaughtExceptionC);
	}()}
{}

template<typename F>
ScopeFailGuard<F>::~ScopeFailGuard() {
	signed const inFlightExceptionCNow_ = std::uncaught_exceptions();
	// i don't think it's possible for the new count of in-flight exceptions to
	// be any lower than or more than 1 higher than the count of exceptions that
	// were in-flight when this guard was created.
	ASSERT(0 <= inFlightExceptionCNow_);
	auto const inFlightExceptionCNow = static_cast<unsigned>(inFlightExceptionCNow_);
	ASSERT(inFlightExceptionCWhenCreated <= inFlightExceptionCNow);
	if(inFlightExceptionCWhenCreated < inFlightExceptionCNow) {
		ASSERT(inFlightExceptionCNow - inFlightExceptionCWhenCreated == 1);
		f();
	}
}
