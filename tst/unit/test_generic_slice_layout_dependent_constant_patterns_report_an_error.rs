//@ compile-fail: generic
#![feature(core_intrinsics)]
use std::marker::PhantomData;
struct Layout<T>(PhantomData<T>);
impl<T> Layout<T> {
    const BYTES: usize = unsafe {
        std::intrinsics::size_of_val(std::ptr::slice_from_raw_parts(std::ptr::null::<T>(), 0))
    };
}
fn matches<T>(value: usize) -> bool {
    match value { Layout::<T>::BYTES => true, _ => false }
}
fn main() { let _ = matches::<u32>(0); }
