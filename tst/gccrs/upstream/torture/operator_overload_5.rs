/* { dg-output "not\r*\n" } */

use std::ops::Not;

struct Number(i32);

impl Not for Number {
    type Output = Number;

    fn not(self) -> Number {
        println!("not");
        Number(!self.0)
    }
}

fn gccrs_main() -> i32 {
    let _ = !Number(1);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
