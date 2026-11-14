//@ env: RUSTC_BOOTSTRAP=1
#![feature(auto_traits, negative_impls)]

use std::ops::Deref;

auto trait Plain {}
impl<T: ?Sized> !Plain for &T {}
impl<T: ?Sized> !Plain for &mut T {}

trait ByValue {
    fn by_value(self) -> u8;
}

impl<T: Plain> ByValue for T {
    fn by_value(self) -> u8 {
        23
    }
}

struct View<T: ?Sized>(T);

impl<T: ?Sized> Deref for View<T> {
    type Target = u8;

    fn deref(&self) -> &u8 {
        &23
    }
}

fn main() {
    let storage = View([1u8, 2]);
    let view: &View<[u8]> = &storage;
    assert_eq!((*view).by_value(), 23);
}
