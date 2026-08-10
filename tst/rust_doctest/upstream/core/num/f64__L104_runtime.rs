// Extracted from library/core/src/num/f64.rs:104
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let min = std::f64::MIN;

    // intended way
    let min = f64::MIN;
}
