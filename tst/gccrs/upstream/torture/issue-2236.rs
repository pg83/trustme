use std::ops::Deref;

fn foo<T: Deref<Target = i32>>(value: T) -> i32 {
    (*value).max(2)
}

fn gccrs_main() -> i32 {
    let value = 1i32;
    foo(&value) - 2
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
