// Extracted from library/core/src/num/f64.rs:230
#![allow(unused)]
fn main() {
    // deprecated way
    #[allow(deprecated, deprecated_in_future)]
    let nan = std::f64::NAN;

    // intended way
    let nan = f64::NAN;
}
