#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;

trait Provider<'a> {
    type Item;
    fn read(&'a self) -> Self::Item;
}
impl<'a> Provider<'a> for u16 {
    type Item = u8;
    fn read(&'a self) -> u8 { *self as u8 }
}

#[derive(CoercePointee)]
#[repr(transparent)]
struct Handle<'a, T: ?Sized + Provider<'a, Item: Copy>>(&'a T);

fn main() {
    let source = 42u16;
    let handle = Handle(&source);
    let erased: Handle<'_, dyn Provider<'_, Item = u8>> = handle;
    assert_eq!(erased.0.read(), 42);
}
