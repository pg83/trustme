// Extracted from library/core/src/num/f32.rs:104
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let min = std::f32::MIN;

    // intended way
    let min = f32::MIN;
}
