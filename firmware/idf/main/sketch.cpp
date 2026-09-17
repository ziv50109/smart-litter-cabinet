// Include C++ traits before Arduino headers and verify the application dialect.
#include <type_traits>
static_assert(__cplusplus >= 201703L, "The firmware requires C++17 or newer.");
static_assert(std::is_integral_v<unsigned>, "The C++ standard library must provide C++17 type traits.");

// One implementation shared with the Arduino IDE sketch; no duplicate entry point.
#include "../../main/main.ino"
