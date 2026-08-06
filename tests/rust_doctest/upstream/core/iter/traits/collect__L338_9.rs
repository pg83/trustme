// Extracted from library/core/src/iter/traits/collect.rs:338
#![allow(unused)]
fn main() {
    // You can extend a String with some chars:
    let mut message = String::from("The first three letters are: ");
    
    message.extend(&['a', 'b', 'c']);
    
    assert_eq!("abc", &message[29..32]);
}
