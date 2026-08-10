// Extracted from library/core/src/option.rs:1507
#![allow(unused)]
fn main() {
    fn sq_then_to_string(x: u32) -> Option<String> {
        x.checked_mul(x).map(|sq| sq.to_string())
    }

    assert_eq!(Some(2).and_then(sq_then_to_string), Some(4.to_string()));
    assert_eq!(Some(1_000_000).and_then(sq_then_to_string), None); // overflowed!
    assert_eq!(None.and_then(sq_then_to_string), None);
}
