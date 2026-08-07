// Extracted from library/alloc/src/borrow.rs:218
#![allow(unused)]
#![feature(cow_is_borrowed)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;

    let cow = Cow::Borrowed("moo");
    assert!(cow.is_borrowed());

    let bull: Cow<'_, str> = Cow::Owned("...moo?".to_string());
    assert!(!bull.is_borrowed());
}
