// Extracted from library/core/src/num/f64.rs:804
#![allow(unused)]
fn main() {
    let x = 1.0f64;
    // Clamp value into range [0, 1).
    let clamped = x.clamp(0.0, 1.0f64.next_down());
    assert!(clamped < 1.0);
    assert_eq!(clamped.next_up(), 1.0);
}
