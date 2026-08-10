// Extracted from src/types/closure.md:518
#![allow(unused)]
fn main() {
    #[repr(packed)]
    struct T(String, String);
    
    let mut t = T(String::new(), String::new());
    let c = || {
        let a = std::ptr::addr_of!(t.1); // captures `t` with ImmBorrow
    };
    let a = t.0; // ERROR: cannot move out of `t.0` because it is borrowed
    c();
}
