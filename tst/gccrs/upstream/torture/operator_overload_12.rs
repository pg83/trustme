// { dg-output "1\r*\n" }

use std::ops::BitAndAssign;

struct Number(i32);

impl BitAndAssign for Number {
    fn bitand_assign(&mut self, other: Number) {
        self.0 &= other.0;
        println!("{}", self.0);
    }
}

fn gccrs_main() -> i32 {
    let mut value = Number(1);
    value &= Number(1);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
