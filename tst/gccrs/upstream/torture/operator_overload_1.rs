/* { dg-output "3\r*\n" } */

use std::ops::Add;

struct Number(i32);

impl Add for Number {
    type Output = Number;

    fn add(self, other: Number) -> Number {
        let result = Number(self.0 + other.0);
        println!("{}", result.0);
        result
    }
}

fn gccrs_main() -> i32 {
    let _ = Number(1) + Number(2);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
