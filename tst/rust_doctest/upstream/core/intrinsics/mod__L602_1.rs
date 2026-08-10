// Extracted from library/core/src/intrinsics/mod.rs:602
#![allow(unused)]
fn main() {
    fn foo() -> i32 {
        0
    }
    // Crucially, we `as`-cast to a raw pointer before `transmute`ing to a function pointer.
    // This avoids an integer-to-pointer `transmute`, which can be problematic.
    // Transmuting between raw pointers and function pointers (i.e., two pointer types) is fine.
    let pointer = foo as *const ();
    let function = unsafe {
        std::mem::transmute::<*const (), fn() -> i32>(pointer)
    };
    assert_eq!(function(), 0);
}
