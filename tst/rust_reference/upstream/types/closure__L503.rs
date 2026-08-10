// Extracted from src/types/closure.md:503
#![allow(unused)]
fn main() {
    #[repr(packed)]
    struct T(i32, i32);
    
    let t = T(2, 5);
    let c = || {
        let a = t.0; // captures `t` with ImmBorrow
    };
    // Copies out of `t` are ok.
    let (a, b) = (t.0, t.1);
    c();
}
