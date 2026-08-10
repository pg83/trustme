// Extracted from library/core/src/iter/traits/iterator.rs:644
#![allow(unused)]
#![feature(iter_intersperse)]
fn main() {

    let words = ["Hello", "World", "!"];
    let hello: String = words.into_iter().intersperse(" ").collect();
    assert_eq!(hello, "Hello World !");
}
