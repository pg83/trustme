//@ edition: 2024
// In edition 2024 `gen {` opens a `gen` block, also as the first tokens of
// `assert!`'s condition, where no token has been taken from the stream yet:
// the edition checked is the `gen` token's own.
#![feature(gen_blocks)]
fn main() {
    let mut it = gen { yield 1; };
    assert_eq!(it.next(), Some(1));
    assert!(gen { yield 2; }.next() == Some(2));
}
