// Extracted from library/core/src/option.rs:829
#![allow(unused)]
fn main() {
    for i in [Some(1234_u16), None] {
        assert_eq!(i.as_ref(), i.as_slice().first());
    }
}
