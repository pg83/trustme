// Extracted from library/core/src/option.rs:819
#![allow(unused)]
fn main() {
    assert_eq!(
        [Some(1234).as_slice(), None.as_slice()],
        [&[1234][..], &[][..]],
    );
}
