// Extracted from library/core/src/str/iter.rs:902
#![allow(unused)]
#![feature(str_split_remainder)]
fn main() {
    let mut split = "A..B..".rsplit_terminator('.');
    assert_eq!(split.remainder(), Some("A..B.."));
    split.next();
    assert_eq!(split.remainder(), Some("A..B"));
    split.by_ref().for_each(drop);
    assert_eq!(split.remainder(), None);
}
