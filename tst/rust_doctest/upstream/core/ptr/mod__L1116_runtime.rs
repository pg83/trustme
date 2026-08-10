// Extracted from library/core/src/ptr/mod.rs:1116
#![allow(unused)]
fn main() {
    use std::ptr;
    type T = i32;
    fn foo() -> T { 42 }
    let mut x = foo();
    let p = ptr::from_mut(&mut x);
    unsafe { p.write(T::default()) };
}
