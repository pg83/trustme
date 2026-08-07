// Extracted from library/core/src/num/f32.rs:266
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let ninf = std::f32::NEG_INFINITY;

    // intended way
    let ninf = f32::NEG_INFINITY;
}
