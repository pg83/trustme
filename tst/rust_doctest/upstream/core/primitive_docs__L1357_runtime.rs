// Extracted from library/core/src/primitive_docs.rs:1357
#![allow(unused)]
#![allow(unused_assignments)]
fn main() {
    let mut x: f32 = 0.0;
    let a: f32 = 1.0;
    let b: f32 = 2.0;
    let c: f32 = 3.0;
    let d: f32 = 4.0;
    x = a + b + c + d; // As written
    x = (a + c) + (b + d); // Reordered to shorten critical path and enable vectorization
}
