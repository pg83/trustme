
#![feature(lang_items)]

macro_rules! impl_foo {
	() => { impl Foo }
}

pub trait Foo {}

pub trait Bar {
    type Baz;
}

struct MyBaz; // { dg-warning "struct is never constructed" }
impl Foo for MyBaz {}

struct MyBar;

impl Bar for MyBar {
    type Baz = MyBaz;
}

pub fn foo(_value: impl Bar<Baz = impl_foo!()>) -> i32 {
    15
}

fn gccrs_main() -> i32 {
    let bar = MyBar;
    let result: i32 = foo(bar);

    result - 15
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
