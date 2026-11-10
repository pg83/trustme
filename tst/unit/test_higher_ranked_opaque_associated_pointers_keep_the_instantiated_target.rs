#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]

trait View<T: ?Sized> {
    type Pointer;
    fn view(&self, value: &T) -> Self::Pointer;
}

struct Source;
impl<T: ?Sized> View<T> for Source {
    type Pointer = *const T;
    fn view(&self, value: &T) -> Self::Pointer {
        value as *const T
    }
}

fn source() -> impl for<T> View<T, Pointer = *const T> {
    Source
}

fn check<F: for<T> View<T, Pointer = *const T>>(factory: &F) {
    let byte = 37u8;
    let word = 541u16;
    let byte_pointer = <F as View<u8>>::view(factory, &byte);
    let word_pointer = <F as View<u16>>::view(factory, &word);
    assert_eq!(byte_pointer, &byte as *const u8);
    assert_eq!(word_pointer, &word as *const u16);
    assert_eq!(unsafe { (*byte_pointer as u16) + *word_pointer }, 578);
}

fn main() {
    check(&source());
}
