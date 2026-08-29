#include "ast_edition.h"

#include "output.h"

using namespace stl;

template <>
void stl::output<ZeroCopyOutput, ASTEdition>(ZeroCopyOutput& os, ASTEdition e) {
    switch (e) {
        case ASTEdition::Rust2015:
            os << StringView("Rust2015");
            break;
        case ASTEdition::Rust2018:
            os << StringView("Rust2018");
            break;
        case ASTEdition::Rust2021:
            os << StringView("Rust2021");
            break;
        case ASTEdition::Rust2024:
            os << StringView("Rust2024");
            break;
    }
    return;
}
