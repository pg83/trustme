// Extracted from library/alloc/src/borrow.rs:240
#![allow(unused)]
#![feature(cow_is_borrowed)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;

    let cow: Cow<'_, str> = Cow::Owned("moo".to_string());
    assert!(cow.is_owned());

    let bull = Cow::Borrowed("...moo?");
    assert!(!bull.is_owned());
}
