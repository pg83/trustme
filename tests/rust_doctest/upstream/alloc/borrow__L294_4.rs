// Extracted from library/alloc/src/borrow.rs:294
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;

    let s = "Hello world!";
    let cow = Cow::Borrowed(s);

    assert_eq!(
      cow.into_owned(),
      String::from(s)
    );
}
