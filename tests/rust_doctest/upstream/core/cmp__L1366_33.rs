// Extracted from library/core/src/cmp.rs:1366
#![allow(unused)]
fn main() {
    let result = f64::NAN.partial_cmp(&1.0);
    assert_eq!(result, None);
}
