// Extracted from library/core/src/iter/adapters/mod.rs:91
#![allow(unused)]
#![feature(inplace_iteration)]
fn main() {
    use std::iter::SourceIter;
    
    let mut iter = vec![9, 9, 9].into_iter().map(|i| i * i);
    let _ = iter.next();
    let mut remainder = std::mem::replace(unsafe { iter.as_inner() }, Vec::new().into_iter());
    println!("n = {} elements remaining", remainder.len());
}
