use std::ops::Add;

pub fn foo<T: Add<Output = i32> + Copy>(a: T) -> i32 {
    a + a
}

fn gccrs_main() -> i32 {
    foo(1) - 2
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
