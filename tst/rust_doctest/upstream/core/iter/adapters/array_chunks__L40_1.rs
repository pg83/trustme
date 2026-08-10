// Extracted from library/core/src/iter/adapters/array_chunks.rs:40
#![allow(unused)]
#![feature(iter_array_chunks)]
fn main() {
    // Also serves as a regression test for https://github.com/rust-lang/rust/issues/123333
    let x = [1,2,3,4,5].into_iter().array_chunks::<2>();
    let mut rem = x.into_remainder().unwrap();
    assert_eq!(rem.next(), Some(5));
    assert_eq!(rem.next(), None);
}
