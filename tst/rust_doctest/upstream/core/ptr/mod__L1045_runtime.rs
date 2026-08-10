// Extracted from library/core/src/ptr/mod.rs:1045
#![allow(unused)]
fn main() {
    type T = i32;
    fn foo() -> T { 42 }
    // The temporary holding the return value of `foo` has its lifetime extended,
    // because the surrounding expression involves no function call.
    let p = &foo() as *const T;
    unsafe { p.read() };
}
