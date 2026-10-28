#![feature(specialization)]
#![allow(incomplete_features)]

trait Choice {
    const VALUE: u8;
    fn tag() -> u8;
}
struct Wrap<T>(T);

impl<T> Choice for Wrap<T> {
    default const VALUE: u8 = 1;
    default fn tag() -> u8 { 10 }
}
impl<T> Choice for Wrap<Wrap<T>> {
    default const VALUE: u8 = 2;
}
impl Choice for Wrap<Wrap<u8>> {
    fn tag() -> u8 { 20 }
}

impl<T> Choice for (T, u8) {
    default const VALUE: u8 = 3;
    default fn tag() -> u8 { 30 }
}
impl<T> Choice for ((T,), u8) {
    default const VALUE: u8 = 4;
}
impl Choice for ((u8,), u8) {
    fn tag() -> u8 { 40 }
}

fn main() {
    assert_eq!((Wrap::<u16>::VALUE, Wrap::<u16>::tag()), (1, 10));
    assert_eq!((Wrap::<Wrap<u16>>::VALUE, Wrap::<Wrap<u16>>::tag()), (2, 10));
    assert_eq!((Wrap::<Wrap<u8>>::VALUE, Wrap::<Wrap<u8>>::tag()), (2, 20));
    assert_eq!((<(u16, u8)>::VALUE, <(u16, u8)>::tag()), (3, 30));
    assert_eq!((<((u16,), u8)>::VALUE, <((u16,), u8)>::tag()), (4, 30));
    assert_eq!((<((u8,), u8)>::VALUE, <((u8,), u8)>::tag()), (4, 40));
}
