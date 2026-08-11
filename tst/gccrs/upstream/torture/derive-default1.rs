#[derive(Default)]
struct Foo {
    a: i32,
}

#[derive(Default)]
struct Bar(i32);

fn gccrs_main() -> i32 {
    let foo = Foo::default();
    let bar = Bar::default();

    foo.a + bar.0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
