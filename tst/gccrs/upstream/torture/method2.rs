// { dg-output "foo_deref\r*\nimm_deref\r*\n" }

use std::ops::Deref;

struct Bar(i32);
impl Bar { fn foobar(&self) -> i32 { self.0 } }

struct Trace<T> { value: T, message: &'static str }
impl<T> Deref for Trace<T> {
    type Target = T;
    fn deref(&self) -> &T { println!("{}", self.message); &self.value }
}

fn gccrs_main() -> i32 {
    let value = Trace {
        value: Trace { value: Bar(123), message: "imm_deref" },
        message: "foo_deref",
    };
    value.foobar() - 123
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
