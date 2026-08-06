// Extracted from library/core/src/array/mod.rs:606
#![allow(unused)]
fn main() {
    let floats = [3.1, 2.7, -1.0];
    let float_refs: [&f64; 3] = floats.each_ref();
    assert_eq!(float_refs, [&3.1, &2.7, &-1.0]);
}
