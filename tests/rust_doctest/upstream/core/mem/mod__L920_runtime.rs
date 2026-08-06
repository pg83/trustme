// Extracted from library/core/src/mem/mod.rs:920
#![allow(unused)]
fn main() {
    let v = vec![1, 2, 3];
    
    drop(v); // explicitly drop the vector
}
