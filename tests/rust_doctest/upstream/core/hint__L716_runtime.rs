// Extracted from library/core/src/hint.rs:716
#![allow(unused)]
#![feature(cold_path)]
fn main() {
    use core::hint::cold_path;
    
    fn foo(x: &[i32]) {
        if let Some(first) = x.get(0) {
            // this is the fast path
        } else {
            // this path is unlikely
            cold_path();
        }
    }
    
    fn bar(x: i32) -> i32 {
        match x {
            1 => 10,
            2 => 100,
            3 => { cold_path(); 1000 }, // this branch is unlikely
            _ => { cold_path(); 10000 }, // this is also unlikely
        }
    }
}
