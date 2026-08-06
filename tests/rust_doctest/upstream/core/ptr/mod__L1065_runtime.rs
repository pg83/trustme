// Extracted from library/core/src/ptr/mod.rs:1065
#![allow(unused)]
fn main() {
    use std::ptr;
    type T = i32;
    fn foo() -> T { 42 }
    let x = foo();
    let p = ptr::from_ref(&x);
    unsafe { p.read() };
}
