// Extracted from library/alloc/src/string.rs:3135
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::borrow::Cow;
    // If the string is not owned...
    let cow: Cow<'_, str> = Cow::Borrowed("eggplant");
    // It will allocate on the heap and copy the string.
    let owned: String = String::from(cow);
    assert_eq!(&owned[..], "eggplant");
}
