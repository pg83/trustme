//@ aux-build: hygienic_associated_constant_trait.rs
#![feature(decl_macro)]

struct Local;

macro implement($target:ty) {
    impl hygienic_associated_constant_trait::Counter for $target {
        const VALUE: u16 = 41;
    }
}

implement!(Local);

fn main() {
    assert_eq!(<Local as hygienic_associated_constant_trait::Counter>::VALUE, 41);
}
