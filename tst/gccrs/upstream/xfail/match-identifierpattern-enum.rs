
enum Foo {
    I(i32),
}

fn gccrs_main() -> i32 {
    let x = Foo::I(0);
    let mut ret = 1;

    match x {
        _whole @ Foo::I(b) => { ret = b },
        _ => {},
    };

    ret
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
