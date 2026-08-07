// Extracted from library/alloc/src/borrow.rs:309
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;

    let s = "Hello world!";
    let cow: Cow<'_, str> = Cow::Owned(String::from(s));

    assert_eq!(
      cow.into_owned(),
      String::from(s)
    );
}
