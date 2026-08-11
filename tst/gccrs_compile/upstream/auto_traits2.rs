
use std::marker::{Send, Sync};

trait A {
    fn a_method(&self) {}
}

fn foo(a: &(dyn A + Send + Sync)) {
    a.a_method();
}

struct S;

impl A for S {
    fn a_method(&self) {}
}

fn main() {
    let s = S;

    foo(&s);
}
