#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;

trait Related<T: ?Sized> {}
impl<T: ?Sized> Related<T> for u32 {}
trait Provider { type Item; fn read(&self) -> Self::Item; }
impl Provider for u32 { type Item = u32; fn read(&self) -> u32 { *self } }

#[derive(CoercePointee)]
#[repr(transparent)]
struct Handle<'a, T: ?Sized + Provider<Item: Related<T>>>(&'a T);

fn main() {
    let value = 53u32;
    let erased: Handle<'_, dyn Provider<Item = u32>> = Handle(&value);
    assert_eq!(erased.0.read(), 53);
}
