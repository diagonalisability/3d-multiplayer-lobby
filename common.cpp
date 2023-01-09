#include <cstdlib>
#include <iostream>
void error(char const *message) {
	std::cerr << message;
	std::exit(1);
}
