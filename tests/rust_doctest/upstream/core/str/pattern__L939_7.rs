// Extracted from library/core/src/str/pattern.rs:939
#![allow(unused)]
fn main() {
    assert_eq!("Hello world".find(char::is_uppercase), Some(0));
    assert_eq!("Hello world".find(|c| "aeiou".contains(c)), Some(1));
}
