// Extracted from library/core/src/num/f64.rs:122
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let min = std::f64::MIN_POSITIVE;
    
    // intended way
    let min = f64::MIN_POSITIVE;
}
