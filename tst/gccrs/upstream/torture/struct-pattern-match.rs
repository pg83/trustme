
enum Foo {
    A { x: i32 },
    B { y: i32 }
}

fn gccrs_main() -> i32 {
    let x = Foo::A { x: 12 };
    match x {
        Foo::A { x: 10 } => 1,
        Foo::B { y: 11 } => 2,
        Foo::A { x: abc } => abc - 12,
        _ => 3,
    }
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
