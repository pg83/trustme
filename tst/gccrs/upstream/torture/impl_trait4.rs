
#![feature(lang_items)]

trait Foo {
    fn id(&self) -> i32;
}

struct A(i32);
struct B(i32);

impl Foo for A {
    fn id(&self) -> i32 {
        self.0
    }
}

impl Foo for B {
    fn id(&self) -> i32 {
        self.0
    }
}

fn takes_tuple(pair: (impl Foo, impl Foo)) -> i32 {
    pair.0.id() + pair.1.id()
}

fn gccrs_main() -> i32 {
    let a = A(1);
    let b = B(2);
    takes_tuple((a, b)) - 3
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
