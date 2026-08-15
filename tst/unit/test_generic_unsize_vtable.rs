#![feature(unsize)]

use core::marker::Unsize;
use core::fmt::Display;

fn unsize_ref<T: ?Sized, U: Unsize<T>>(value: &U) -> &T {
    value
}

fn main() {
    let value = 4i32;
    let object = unsize_ref::<dyn Display, _>(&value);
    assert_eq!(object.to_string(), "4");
}
