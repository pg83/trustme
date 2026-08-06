// Extracted from library/core/src/hint.rs:84
#![allow(unused)]
fn main() {
    fn div_1(a: u32, b: u32) -> u32 {
        use std::hint::unreachable_unchecked;
    
        // `b.saturating_add(1)` is always positive (not zero),
        // hence `checked_div` will never return `None`.
        // Therefore, the else branch is unreachable.
        a.checked_div(b.saturating_add(1))
            .unwrap_or_else(|| unsafe { unreachable_unchecked() })
    }
    
    assert_eq!(div_1(7, 0), 7);
    assert_eq!(div_1(9, 1), 4);
    assert_eq!(div_1(11, u32::MAX), 0);
}
