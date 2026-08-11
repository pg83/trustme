
#![feature(lang_items)]

pub struct MyBuf;

trait Foo {
    type Bar<T>: Sized;
}

impl Foo for MyBuf {
    type Bar<T> = T;
}

type A = <MyBuf as Foo>::Bar<u32>;
fn gccrs_main() -> i32 {
    let a: A = 1;
    a as i32 - 1
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
