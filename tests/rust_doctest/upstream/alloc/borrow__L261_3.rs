// Extracted from library/alloc/src/borrow.rs:261
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;
    
    let mut cow = Cow::Borrowed("foo");
    cow.to_mut().make_ascii_uppercase();
    
    assert_eq!(
      cow,
      Cow::Owned(String::from("FOO")) as Cow<'_, str>
    );
}
