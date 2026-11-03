//@ proc-macro-aux-build: type_fragment_spelling.rs
//@ compile-variant: -O
#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]
macro_rules! forward { ($ty:ty) => { type_fragment_spelling::spelling!($ty) }; }
fn compact(text: &str) -> String {
    text.chars().filter(|ch| !ch.is_whitespace()).collect()
}
fn main() {
    assert_eq!(compact(forward!(dyn for<'a, 'b, T, U> Family<&'a T, &'b U> + Sync)),
        "dynfor<'a,'b,T,U>Family<&'aT,&'bU>+Sync");
    assert_eq!(compact(forward!(dyn for<'a, 'b> Family<&'a u16, &'b u16>)),
        "dynfor<'a,'b>Family<&'au16,&'bu16>");
}
