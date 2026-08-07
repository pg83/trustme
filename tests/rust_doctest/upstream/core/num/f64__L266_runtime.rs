// Extracted from library/core/src/num/f64.rs:266
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let ninf = std::f64::NEG_INFINITY;

    // intended way
    let ninf = f64::NEG_INFINITY;
}
