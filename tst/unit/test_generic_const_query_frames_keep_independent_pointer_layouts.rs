//@ compile-variant: -O
#![feature(core_intrinsics)]
use std::marker::PhantomData;

const fn empty_bytes<T>() -> usize {
    unsafe { std::intrinsics::size_of_val(std::ptr::slice_from_raw_parts(std::ptr::null::<T>(), 0)) }
}
const fn independent<T>() -> usize { empty_bytes::<u32>() }
struct Query<T>(PhantomData<T>);
impl<T> Query<T> {
    const ZERO: usize = independent::<T>();
}
fn matches<T>(value: usize) -> bool {
    match value { Query::<T>::ZERO => true, _ => false }
}
fn main() {
    assert!(matches::<String>(0));
    assert!(!matches::<u8>(1));
    assert_eq!(const { empty_bytes::<u16>() }, 0);
    assert_eq!(const { empty_bytes::<[u64; 9]>() }, 0);
}
