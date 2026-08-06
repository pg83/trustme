// Extracted from library/core/src/option.rs:867
#![allow(unused)]
fn main() {
    assert_eq!(
        [Some(1234).as_mut_slice(), None.as_mut_slice()],
        [&mut [1234][..], &mut [][..]],
    );
}
