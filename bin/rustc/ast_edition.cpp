#include "ast_edition.h"

namespace AST {

std::ostream& operator<<(std::ostream& os, const Edition& e) {
    switch (e) {
        case Edition::Rust2015:
            os << "Rust2015";
            break;
        case Edition::Rust2018:
            os << "Rust2018";
            break;
        case Edition::Rust2021:
            os << "Rust2021";
            break;
        case Edition::Rust2024:
            os << "Rust2024";
            break;
    }
    return os;
}
}
