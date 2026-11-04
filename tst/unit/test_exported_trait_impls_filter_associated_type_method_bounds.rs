//@ aux-build: public_trait_method_bounds.rs
//@ compile-variant: -O
use public_trait_method_bounds::{Live, Select};
fn main() {
    assert_eq!(<Live as Select>::value(), 47);
    assert_eq!(<Live as Select>::lifetime(), 61);
    assert_eq!(public_trait_method_bounds::answer(), 108);
}
