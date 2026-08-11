/* { dg-output "neg\r*\n" } */

use std::ops::Neg;

struct Number(i32);

impl Neg for Number {
    type Output = Number;

    fn neg(self) -> Number {
        println!("neg");
        Number(-self.0)
    }
}

fn gccrs_main() -> i32 {
    let _ = -Number(1);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
