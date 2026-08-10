// Extracted from library/core/src/num/f32.rs:25
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let r = std::f32::RADIX;

    // intended way
    let r = f32::RADIX;
}
