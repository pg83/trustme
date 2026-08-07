// Extracted from library/core/src/str/iter.rs:1204
#![allow(unused)]
#![feature(str_lines_remainder)]
fn main() {

    let mut lines = "a\nb\nc\nd".lines();
    assert_eq!(lines.remainder(), Some("a\nb\nc\nd"));

    lines.next();
    assert_eq!(lines.remainder(), Some("b\nc\nd"));

    lines.by_ref().for_each(drop);
    assert_eq!(lines.remainder(), None);
}
