// Extracted from src/types/closure.md:477
#![allow(unused)]
fn main() {
    union U {
        a: (i32, i32),
        b: bool,
    }
    let u = U { a: (123, 456) };
    
    let c = || {
        let x = unsafe { u.a.0 }; // captures `u` ByValue
    };
    c();
    
    // This also includes writing to fields.
    let mut u = U { a: (123, 456) };
    
    let mut c = || {
        u.b = true; // captures `u` with MutBorrow
    };
    c();
}
