// Extracted from library/core/src/num/f32.rs:787
#![allow(unused)]
fn main() {
    let x = 1.0f32;
    // Clamp value into range [0, 1).
    let clamped = x.clamp(0.0, 1.0f32.next_down());
    assert!(clamped < 1.0);
    assert_eq!(clamped.next_up(), 1.0);
}
