/* { dg-output "foo_deref\r*\nimm_deref\r*\n123\r*\n" } */

use std::ops::Deref;

struct Trace<T> { value: T, message: &'static str }

impl<T> Deref for Trace<T> {
    type Target = T;
    fn deref(&self) -> &T { println!("{}", self.message); &self.value }
}

fn gccrs_main() -> i32 {
    let value = Trace { value: Trace { value: 123, message: "imm_deref" }, message: "foo_deref" };
    let result: &i32 = &value;
    println!("{}", *result);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
