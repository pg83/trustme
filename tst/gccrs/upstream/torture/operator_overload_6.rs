/* { dg-output "add_assign\r*\n3\r*\n" } */

use std::ops::AddAssign;

struct Number(i32);

impl AddAssign for Number {
    fn add_assign(&mut self, other: Number) {
        println!("add_assign");
        self.0 += other.0;
    }
}

fn gccrs_main() -> i32 {
    let mut result = Number(1);
    result += Number(2);
    println!("{}", result.0);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
