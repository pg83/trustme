#pragma once

/*
 */
#include <iostream>

namespace AST {

    enum class Edition {
        Rust2015,
        Rust2018,
        Rust2021,
        Rust2024,
    };

    std::ostream& operator<<(std::ostream& os, const Edition& e);

}
