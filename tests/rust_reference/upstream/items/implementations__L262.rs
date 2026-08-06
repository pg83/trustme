// Extracted from src/items/implementations.md:262
#![allow(unused)]
fn main() {
    struct Struct;
    trait HasAssocType { type Ty; }
    impl<'a> HasAssocType for Struct {
        type Ty = &'a Struct;
    }
}
