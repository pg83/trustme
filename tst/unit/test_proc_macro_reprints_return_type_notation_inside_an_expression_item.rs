//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
#![feature(return_type_notation)]

use proc_macro_item_passthrough::echo;

trait Factory {
    fn make() -> impl Copy;
}

macro_rules! roundtrip {
    ($expression:expr) => {
        echo!($expression)
    };
}

fn main() {
    let value = roundtrip!({
        fn require_copy<T: Factory<make(..): Copy>>() {}
        (61u32, std::mem::size_of::<u64>())
    });
    assert_eq!(value, (61, 8));
}
