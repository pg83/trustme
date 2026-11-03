//@ proc-macro-aux-build: type_fragment_spelling.rs
//@ compile-variant: -O
#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]
macro_rules! forward { ($ty:ty) => { type_fragment_spelling::spelling!($ty) }; }
fn compact(text: &str) -> String {
    text.chars().filter(|ch| !ch.is_whitespace()).collect()
}
fn main() {
    assert_eq!(compact(forward!(dyn for<T, U> Family<T, U> + Send)),
        "dynfor<T,U>Family<T,U>+Send");
    assert_eq!(compact(forward!(dyn for<'a, T> Family<&'a T>)),
        "dynfor<'a,T>Family<&'aT>");
}
