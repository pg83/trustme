//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
#![feature(return_type_notation)]

use proc_macro_item_passthrough::echo;

trait Factory {
    fn make() -> impl Into<u64>;
}

struct Source;

impl Factory for Source {
    #[allow(refining_impl_trait)]
    fn make() -> u64 {
        47
    }
}

fn duplicate<T: Copy>(value: T) -> (T, T) {
    (value, value)
}

macro_rules! roundtrip {
    ($bound:path) => {
        echo! {
            fn collect<T: $bound>() -> u64 {
                let (left, right) = duplicate(T::make());
                let left: u64 = left.into();
                let right: u64 = right.into();
                left + right
            }
        }
        const SPELLING: &str = stringify!($bound);
    };
}

roundtrip!(Factory<make(..): Copy>);

fn main() {
    assert_eq!(collect::<Source>(), 94);
    assert_eq!(SPELLING.split_whitespace().collect::<String>(), "Factory<make(..):Copy>");
}
