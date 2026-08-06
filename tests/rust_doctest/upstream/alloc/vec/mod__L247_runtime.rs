// Extracted from library/alloc/src/vec/mod.rs:247
#![allow(unused)]
extern crate alloc;
fn main() {
    fn read_slice(slice: &[usize]) {
        // ...
    }
    
    let v = vec![0, 1];
    read_slice(&v);
    
    // ... and that's all!
    // you can also do it like this:
    let u: &[usize] = &v;
    // or like this:
    let u: &[_] = &v;
}
