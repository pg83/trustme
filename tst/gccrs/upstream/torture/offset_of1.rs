// { dg-do run { target x86_64*-*-* } }
// { dg-additional-options "-frust-compat-version=1.71" }


pub struct Foo {
    pub a: i32,
    pub b: i32,
}

fn gccrs_main() -> i32 {
    let a = std::mem::offset_of!(Foo, a); // valid
    let b = std::mem::offset_of!(Foo, b); // valid

    let res = a + b - 4;

    res as i32
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
