//@ compile-fail: error
#![feature(non_lifetime_binders, sized_hierarchy)]
#![allow(incomplete_features)]
use std::marker::PointeeSized;

trait View<T: PointeeSized> {
    type Pointer;
    fn view(&self, value: &T) -> Self::Pointer;
}

trait Store {
    type Access: for<T> View<T, Pointer = *const T>;
    fn access() -> Self::Access;
}

fn wrong<S: Store>() -> *const u32 {
    let access = S::access();
    let byte = 71u8;
    <S::Access as View<u8>>::view(&access, &byte)
}

fn main() {}
