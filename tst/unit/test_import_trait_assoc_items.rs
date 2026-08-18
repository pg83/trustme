// The associated items of a trait can be imported by name and by glob; each
// stands for the path through the trait.
#![feature(import_trait_associated_functions)]

use A::{DEFAULT, new};
use B::*;
use std::default::Default::default;

trait A: Sized {
    const DEFAULT: Option<Self> = None;
    fn new() -> Self;
}

trait B {
    fn twice(self) -> Self;
}

struct S(u32);

impl A for S {
    fn new() -> Self {
        S(1)
    }
}

impl B for S {
    fn twice(self) -> Self {
        S(self.0 * 2)
    }
}

fn main() {
    let s: S = new();
    assert_eq!(s.0, 1);
    assert_eq!(twice(S(4)).0, 8);
    let none: Option<S> = DEFAULT;
    assert!(none.is_none());
    let zero: u32 = default();
    assert_eq!(zero, 0);
}
