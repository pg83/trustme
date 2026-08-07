// Extracted from library/alloc/src/string.rs:3157
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;
    assert_eq!(Cow::from("eggplant"), Cow::Borrowed("eggplant"));
}
