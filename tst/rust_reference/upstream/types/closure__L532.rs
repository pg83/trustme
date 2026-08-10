// Extracted from src/types/closure.md:532
#![allow(unused)]
fn main() {
    struct T(String, String);
    
    let mut t = T(String::new(), String::new());
    let c = || {
        let a = std::ptr::addr_of!(t.1); // captures `t.1` with ImmBorrow
    };
    // The move here is allowed.
    let a = t.0;
    c();
}
