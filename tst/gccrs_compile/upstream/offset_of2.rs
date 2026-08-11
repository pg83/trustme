// { dg-additional-options "-frust-compile-until=compilation -frust-compat-version=1.71" }


pub struct Foo {
    a: i32,
}

fn main() {
    let _ = std::mem::offset_of!(Foo, a); // valid
}
