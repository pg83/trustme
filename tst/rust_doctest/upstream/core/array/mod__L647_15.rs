// Extracted from library/core/src/array/mod.rs:647
#![allow(unused)]
fn main() {
    let mut floats = [3.1, 2.7, -1.0];
    let float_refs: [&mut f64; 3] = floats.each_mut();
    *float_refs[0] = 0.0;
    assert_eq!(float_refs, [&mut 0.0, &mut 2.7, &mut -1.0]);
    assert_eq!(floats, [0.0, 2.7, -1.0]);
}
