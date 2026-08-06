// Extracted from library/core/src/iter/traits/collect.rs:405
#![allow(unused)]
fn main() {
    // You can extend a String with some chars:
    let mut message = String::from("abc");
    
    message.extend(['d', 'e', 'f'].iter());
    
    assert_eq!("abcdef", &message);
}
