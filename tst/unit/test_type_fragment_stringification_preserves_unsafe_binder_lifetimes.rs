//@ compile-variant: -O
#![feature(unsafe_binders)]
#![allow(incomplete_features)]
macro_rules! text { ($ty:ty) => { stringify!($ty) }; }
fn main() {
    let printed = text!(unsafe<'a> impl Clone + 'a);
    assert!(printed.contains("unsafe"));
    assert!(printed.contains("'a"));
    assert!(printed.contains("Clone"));
}
