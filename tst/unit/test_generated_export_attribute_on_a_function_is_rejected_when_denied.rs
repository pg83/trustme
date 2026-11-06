//@ compile-fail: #[macro_export]
#![deny(unused_attributes)]

macro_rules! declare_function {
    () => {
        #[macro_export]
        fn ordinary_function() {}
    };
}

declare_function!();

fn main() {}
