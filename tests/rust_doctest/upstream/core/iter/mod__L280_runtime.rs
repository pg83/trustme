// Extracted from library/core/src/iter/mod.rs:280
#![allow(unused)]
#![allow(unused_must_use)]
#![allow(map_unit_fn)]
fn main() {
    let v = vec![1, 2, 3, 4, 5];
    v.iter().map(|x| println!("{x}"));
}
