// Extracted from library/core/src/primitive_docs.rs:1344
#![allow(unused)]
#![feature(float_algebraic)]
#![allow(unused_assignments)]
fn main() {
    let mut x: f32 = 0.0;
    let a: f32 = 1.0;
    let b: f32 = 2.0;
    let c: f32 = 3.0;
    let d: f32 = 4.0;
    x = a.algebraic_add(b).algebraic_add(c).algebraic_add(d);
}
