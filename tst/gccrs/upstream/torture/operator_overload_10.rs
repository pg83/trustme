/* { dg-output "foo_deref\r*\n123\r*\n" } */
use std::ops::Deref;
struct Foo(i32);
impl Deref for Foo { type Target = i32; fn deref(&self) -> &i32 { println!("foo_deref"); &self.0 } }
fn gccrs_main() -> i32 { let value = Foo(123); println!("{}", *value); 0 }
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
