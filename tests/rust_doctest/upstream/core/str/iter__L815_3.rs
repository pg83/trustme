// Extracted from library/core/src/str/iter.rs:815
#![allow(unused)]
#![feature(str_split_remainder)]
fn main() {
    let mut split = "Mary had a little lamb".split(' ');
    assert_eq!(split.remainder(), Some("Mary had a little lamb"));
    split.next();
    assert_eq!(split.remainder(), Some("had a little lamb"));
    split.by_ref().for_each(drop);
    assert_eq!(split.remainder(), None);
}
