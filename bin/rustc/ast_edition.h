#pragma once

/*
 */
#include <iostream>

enum class ASTEdition {
    Rust2015,
    Rust2018,
    Rust2021,
    Rust2024,
};

std::ostream& operator<<(std::ostream& os, const ASTEdition& e);
