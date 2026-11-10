#![feature(non_lifetime_binders, sized_hierarchy)]
#![allow(incomplete_features)]
use std::marker::PointeeSized;

trait View<T: PointeeSized> {
    type Pointer;
    fn view(&self, value: &T) -> Self::Pointer;
}

struct Access;
impl<T: PointeeSized> View<T> for Access {
    type Pointer = *const T;
    fn view(&self, value: &T) -> Self::Pointer {
        value as *const T
    }
}

trait Store {
    type Access: for<T> View<T, Pointer = *const T>;
    fn access() -> Self::Access;
}

impl Store for () {
    type Access = Access;
    fn access() -> Self::Access { Access }
}

fn check<S: Store>() {
    let access = S::access();
    let byte = 59u8;
    let word = 1201u32;
    let byte_pointer = <S::Access as View<u8>>::view(&access, &byte);
    let word_pointer = <S::Access as View<u32>>::view(&access, &word);
    assert_eq!(byte_pointer, &byte as *const u8);
    assert_eq!(word_pointer, &word as *const u32);
    assert_eq!(unsafe { (*byte_pointer as u32) + *word_pointer }, 1260);
}

fn main() {
    check::<()>();
}
