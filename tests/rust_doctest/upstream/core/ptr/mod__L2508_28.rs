// Extracted from library/core/src/ptr/mod.rs:2508
#![allow(unused)]
fn main() {
    use std::ptr;
    
    fn a() { println!("a"); }
    fn b() { println!("b"); }
    assert!(!ptr::fn_addr_eq(a as fn(), b as fn()));
}
