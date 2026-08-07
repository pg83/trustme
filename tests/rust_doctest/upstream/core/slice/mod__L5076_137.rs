// Extracted from library/core/src/slice/mod.rs:5076
#![allow(unused)]
#![feature(sort_floats)]
fn main() {
    let mut v = [2.6, -5e-8, f64::NAN, 8.29, f64::INFINITY, -1.0, 0.0, -f64::INFINITY, -0.0];

    v.sort_floats();
    let sorted = [-f64::INFINITY, -1.0, -5e-8, -0.0, 0.0, 2.6, 8.29, f64::INFINITY, f64::NAN];
    assert_eq!(&v[..8], &sorted[..8]);
    assert!(v[8].is_nan());
}
