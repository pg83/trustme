//@ compile-variant: -O
#![feature(coerce_unsized, unsize)]
use std::marker::Unsize;
use std::ops::CoerceUnsized;
struct Handle<T: ?Sized>(*const T);
impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Handle<U>> for Handle<T> {}
trait Value { fn value(&self) -> u16; }
struct Number(u16);
impl Value for Number { fn value(&self) -> u16 { self.0 } }
static ARRAY: [u16; 3] = [13, 17, 19];
static NUMBER: Number = Number(31);
const THIN_ARRAY: Handle<[u16; 3]> = Handle(&ARRAY);
const ARRAY_HANDLE: Handle<[u16]> = THIN_ARRAY;
const THIN_NUMBER: Handle<Number> = Handle(&NUMBER);
const VALUE_HANDLE: Handle<dyn Value> = THIN_NUMBER;
fn main() {
    let slice = unsafe { &*ARRAY_HANDLE.0 };
    assert_eq!(slice, &[13, 17, 19]);
    assert_eq!(std::mem::size_of_val(slice), 6);
    let value = unsafe { &*VALUE_HANDLE.0 };
    assert_eq!(value.value(), 31);
    assert_eq!(std::mem::size_of_val(value), 2);
}
