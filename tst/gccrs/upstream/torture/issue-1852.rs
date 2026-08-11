
enum Foo {
    A,
    B(i32),
}

fn gccrs_main() -> i32 {
    let result = Foo::B(123);

    let value = match result {
        Foo::A => 15,
        Foo::B(x) => x,
    };

    value - 123
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
