// tokio's tests put `use std::os::unix::io::{FromRawFd, IntoRawFd};` in the
// body of a `#[tokio::test]` function. The attribute macro is handed the
// whole item, and the item's text was rebuilt with no way to write a `use`
// of more than one path.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item;

mod shapes {
    pub trait Area {
        fn area(&self) -> u32;
    }
    pub trait Perimeter {
        fn perimeter(&self) -> u32;
    }
    pub struct Square(pub u32);
    impl Area for Square {
        fn area(&self) -> u32 {
            self.0 * self.0
        }
    }
    impl Perimeter for Square {
        fn perimeter(&self) -> u32 {
            4 * self.0
        }
    }
}

#[echo_item]
fn measure() -> u32 {
    use crate::shapes::{Area, Perimeter as Around, Square};
    let square = Square(3);
    square.area() + square.perimeter()
}

fn main() {
    assert_eq!(measure(), 21);
}
