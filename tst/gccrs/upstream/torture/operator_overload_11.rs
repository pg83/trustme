// { dg-output "1\r*\n" }

use std::ops::BitAnd;

struct Number(i32);

impl BitAnd for Number {
    type Output = Number;

    fn bitand(self, other: Number) -> Number {
        let result = Number(self.0 & other.0);
        println!("{}", result.0);
        result
    }
}

fn gccrs_main() -> i32 {
    let _ = Number(1) & Number(1);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
