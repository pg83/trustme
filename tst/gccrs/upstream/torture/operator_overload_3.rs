/* { dg-output "3\r*\n3\r*\n" } */

use std::ops::Add;

struct Foo(i32);

impl Add for Foo {
    type Output = Foo;

    fn add(self, other: Foo) -> Foo {
        let value = self.0 + other.0;
        println!("{}", value);
        println!("{}", value);
        Foo(value)
    }
}

fn gccrs_main() -> i32 {
    let _ = Foo(1) + Foo(2);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
