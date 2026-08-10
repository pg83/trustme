// Extracted from library/core/src/num/f32.rs:1395
#![allow(unused)]
fn main() {
    assert!((-3.0f32).clamp(-2.0, 1.0) == -2.0);
    assert!((0.0f32).clamp(-2.0, 1.0) == 0.0);
    assert!((2.0f32).clamp(-2.0, 1.0) == 1.0);
    assert!((f32::NAN).clamp(-2.0, 1.0).is_nan());
}
