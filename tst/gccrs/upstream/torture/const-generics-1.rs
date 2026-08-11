
#![feature(lang_items)]

struct Foo<const N: usize>;

impl Foo<1> {
    fn call(&self) -> i32 {
        10
    }
}

impl Foo<2> {
    fn call(&self) -> i32 {
        20
    }
}

fn gccrs_main() -> i32 {
    let a = Foo::<1> {};
    let b = Foo::<2> {};
    let aa = a.call();
    let bb = b.call();
    bb - aa - 10
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
