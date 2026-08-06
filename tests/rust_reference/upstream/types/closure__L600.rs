// Extracted from src/types/closure.md:600
#![allow(unused)]
fn main() {
    struct S(i32);
    
    let b = Box::new(S(10));
    let c_box = move || {
        let x = (*b).0; // captures `b` with ByValue
    };
}
