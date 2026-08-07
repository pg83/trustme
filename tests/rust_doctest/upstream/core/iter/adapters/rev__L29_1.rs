// Extracted from library/core/src/iter/adapters/rev.rs:29
#![allow(unused)]
#![feature(rev_into_inner)]
fn main() {

    let s = "foobar";
    let mut rev = s.chars().rev();
    assert_eq!(rev.next(), Some('r'));
    assert_eq!(rev.next(), Some('a'));
    assert_eq!(rev.next(), Some('b'));
    assert_eq!(rev.into_inner().collect::<String>(), "foo");
}
