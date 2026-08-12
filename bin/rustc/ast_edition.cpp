#include "ast_edition.h"


std::ostream& operator<<(std::ostream& os, const ASTEdition& e) {
    switch (e) {
        case ASTEdition::Rust2015:
            os << "Rust2015";
            break;
        case ASTEdition::Rust2018:
            os << "Rust2018";
            break;
        case ASTEdition::Rust2021:
            os << "Rust2021";
            break;
        case ASTEdition::Rust2024:
            os << "Rust2024";
            break;
    }
    return os;
}
