// Extracted from library/alloc/src/string.rs:3178
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;
    let s = "eggplant".to_string();
    let s2 = "eggplant".to_string();
    assert_eq!(Cow::from(s), Cow::<'static, str>::Owned(s2));
}
