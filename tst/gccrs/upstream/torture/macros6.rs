
macro_rules! Test {
    ($a:ident, $b:ty) => {
        struct $a($b);
    };
}

Test!(Foo, i32);

fn gccrs_main() -> i32 {
    let a = Foo(123);
    a.0 - 123
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
