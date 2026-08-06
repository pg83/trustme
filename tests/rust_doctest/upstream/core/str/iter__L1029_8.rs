// Extracted from library/core/src/str/iter.rs:1029
#![allow(unused)]
#![feature(str_split_remainder)]
fn main() {
    let mut split = "Mary had a little lamb".rsplitn(3, ' ');
    assert_eq!(split.remainder(), Some("Mary had a little lamb"));
    split.next();
    assert_eq!(split.remainder(), Some("Mary had a little"));
    split.by_ref().for_each(drop);
    assert_eq!(split.remainder(), None);
}
