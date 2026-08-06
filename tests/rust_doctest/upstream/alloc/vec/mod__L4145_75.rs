// Extracted from library/alloc/src/vec/mod.rs:4145
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;
    let o: Cow<'_, [i32]> = Cow::Owned(vec![1, 2, 3]);
    let b: Cow<'_, [i32]> = Cow::Borrowed(&[1, 2, 3]);
    assert_eq!(Vec::from(o), Vec::from(b));
}
