// Extracted from library/core/src/char/methods.rs:605
#![allow(unused)]
fn main() {
    let len = 'A'.len_utf8();
    assert_eq!(len, 1);

    let len = 'ß'.len_utf8();
    assert_eq!(len, 2);

    let len = 'ℝ'.len_utf8();
    assert_eq!(len, 3);

    let len = '💣'.len_utf8();
    assert_eq!(len, 4);
}
