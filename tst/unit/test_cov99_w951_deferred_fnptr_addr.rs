//@ edition: 2021
#![feature(fn_ptr_trait)]

use core::marker::FnPtr;

struct Deferred<T>(T);

impl<T> Deferred<T> {
    const ADDRESS: fn(fn(T)) -> *const () = <fn(T) as FnPtr>::addr;
}

#[inline(never)]
fn address<T>(pointer: fn(T)) -> *const () {
    Deferred::<T>::ADDRESS(pointer)
}

fn target(_: u8) {}

fn main() {
    assert_eq!(address::<u8>(target), target as *const ());
}
