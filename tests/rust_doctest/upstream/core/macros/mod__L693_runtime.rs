// Extracted from library/core/src/macros/mod.rs:693
#![allow(unused)]
fn main() {
    #[allow(dead_code)]
    fn divide_by_three(x: u32) -> u32 { // one of the poorest implementations of x/3
        for i in 0.. {
            if 3*i < i { panic!("u32 overflow"); }
            if x < 3*i { return i-1; }
        }
        unreachable!("The loop should always return");
    }
}
