// Extracted from library/alloc/src/string.rs:1817
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("hello");

    unsafe {
        let vec = s.as_mut_vec();
        assert_eq!(&[104, 101, 108, 108, 111][..], &vec[..]);

        vec.reverse();
    }
    assert_eq!(s, "olleh");
}
