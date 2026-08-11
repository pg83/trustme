use std::ops::Add;

#[derive(Clone, Copy)]
struct Foo(i32);

impl Add for Foo {
    type Output = i32;

    fn add(self, rhs: Foo) -> i32 {
        self.0 + rhs.0
    }
}

fn bar<T: Add<Output = i32> + Copy>(a: T) -> i32 {
    a + a
}

fn gccrs_main() -> i32 {
    bar(Foo(1)) - 2
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
