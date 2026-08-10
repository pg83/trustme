// Extracted from library/core/src/option.rs:1556
#![allow(unused)]
fn main() {
    fn is_even(n: &i32) -> bool {
        n % 2 == 0
    }

    assert_eq!(None.filter(is_even), None);
    assert_eq!(Some(3).filter(is_even), None);
    assert_eq!(Some(4).filter(is_even), Some(4));
}
