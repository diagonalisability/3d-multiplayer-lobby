#pragma once
#include<cstring>
template<typename Src>
void memcpyInspect(void *const dst, Src const &src) {
	std::memcpy(dst, &src, sizeof src);
}
template<typename Dst>
void memcpyInit(Dst &dst, void const *const src) {
	std::memcpy(&dst, src, sizeof dst);
}
