// Extracted from library/core/src/num/f64.rs:25
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let r = std::f64::RADIX;

    // intended way
    let r = f64::RADIX;
}
