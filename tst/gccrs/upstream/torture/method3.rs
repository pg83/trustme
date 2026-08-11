// { dg-output "mut_deref\r*\nfoobar: 123\r*\n" }

use std::ops::{Deref, DerefMut};

struct Bar(i32);
impl Bar { fn foobar(&mut self) -> i32 { self.0 } }

struct Foo<T>(T);
impl<T> Deref for Foo<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T> DerefMut for Foo<T> {
    fn deref_mut(&mut self) -> &mut T { println!("mut_deref"); &mut self.0 }
}

fn gccrs_main() -> i32 {
    let mut value = Foo(Bar(123));
    let result = value.foobar();
    println!("foobar: {}", result);
    result - 123
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
