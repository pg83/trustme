// Extracted from src/types/closure.md:111
#![allow(unused)]
fn main() {
    struct SomeStruct {
        f1: (i32, i32),
    }
    let s = SomeStruct { f1: (1, 2) };
    
    let c = || {
        let x = s.f1.1; // s.f1.1 captured by ImmBorrow
    };
    c();
}
