// Extracted from library/core/src/iter/traits/iterator.rs:698
#![allow(unused)]
#![feature(iter_intersperse)]
fn main() {

    let src = ["Hello", "to", "all", "people", "!!"].iter().copied();

    // The closure mutably borrows its context to generate an item.
    let mut happy_emojis = [" ❤️ ", " 😀 "].into_iter();
    let separator = || happy_emojis.next().unwrap_or(" 🦀 ");

    let result = src.intersperse_with(separator).collect::<String>();
    assert_eq!(result, "Hello ❤️ to 😀 all 🦀 people 🦀 !!");
}
