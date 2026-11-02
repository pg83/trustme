//@ proc-macro-aux-build: type_phase_identity.rs
#![feature(pin_ergonomics)]
#![allow(incomplete_features)]
extern crate type_phase_identity as proxy;

macro_rules! alias {
    ($ty:ty) => { proxy::emit! { type Pinned<'a> = $ty; } };
}

alias!(&'a pin const u16);

fn main() {
    let value = 47u16;
    let pinned: Pinned<'_> = &pin const value;
    assert_eq!(*std::pin::Pin::get_ref(pinned), 47);
}
