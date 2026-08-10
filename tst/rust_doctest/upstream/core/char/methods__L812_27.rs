// Extracted from library/core/src/char/methods.rs:812
#![allow(unused)]
fn main() {
    const CAPITAL_DELTA_IS_LOWERCASE: bool = 'Δ'.is_lowercase();
    assert!(!CAPITAL_DELTA_IS_LOWERCASE);
}
