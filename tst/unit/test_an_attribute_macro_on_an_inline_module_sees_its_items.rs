// async-trait's tests put `#[rustversion::since(1.75)]` on `pub mod
// issue281 { .. }`: an attribute macro on an inline module is handed the
// module with its items - attributes, macro calls and not yet expanded
// attribute macros on them included.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::{echo_item, echo_item_spelled};

macro_rules! make_const {
    ($name:ident, $value:expr) => {
        pub const $name: u8 = $value;
    };
}

#[echo_item_spelled("mod inner")]
pub mod inner {
    use std::fmt::Debug;

    #[derive(Debug)]
    pub struct Point(pub u8);

    #[super::echo_item]
    impl Point {
        pub fn value(&self) -> u8 {
            self.0
        }
    }

    make_const!(SEVEN, 7);

    pub fn show<T: Debug>(t: T) -> String {
        format!("{:?}", t)
    }
}

#[echo_item]
mod empty {}

fn main() {
    assert_eq!(inner::Point(3).value() + inner::SEVEN, 10);
    assert_eq!(inner::show(inner::Point(1)), "Point(1)");
}
