// Extracted from library/core/src/iter/traits/iterator.rs:99
#![allow(unused)]
#![feature(iter_next_chunk)]
fn main() {

    let quote = "not all those who wander are lost";
    let [first, second, third] = quote.split_whitespace().next_chunk().unwrap();
    assert_eq!(first, "not");
    assert_eq!(second, "all");
    assert_eq!(third, "those");
}
