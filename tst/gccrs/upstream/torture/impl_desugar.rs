
#![feature(lang_items)]

pub trait Foo {}

pub trait Bar {
    type Baz;
}

struct MyBaz;
impl Foo for MyBaz {}

struct MyBar;

impl Bar for MyBar {
    type Baz = MyBaz;
}

pub fn foo<T, U>(_value: T) -> i32
where
    T: Bar<Baz = U>,
    U: Foo,
{
    15
}

fn gccrs_main() -> i32 {
    let bar = MyBar;
    let result: i32 = foo::<MyBar, MyBaz>(bar);

    result - 15
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
