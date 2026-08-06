// Extracted from library/core/src/slice/iter.rs:1318
#![allow(unused)]
fn main() {
    let slice = ['r', 'u', 's', 't'];
    let mut iter = slice.windows(2);
    assert_eq!(iter.next(), Some(&['r', 'u'][..]));
    assert_eq!(iter.next(), Some(&['u', 's'][..]));
    assert_eq!(iter.next(), Some(&['s', 't'][..]));
    assert_eq!(iter.next(), None);
}
