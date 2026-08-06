// Extracted from library/core/src/default.rs:119
#![allow(unused)]
fn main() {
    let i: i8 = Default::default();
    let (x, y): (Option<String>, f64) = Default::default();
    let (a, b, (c, d)): (i32, u32, (bool, bool)) = Default::default();
}
