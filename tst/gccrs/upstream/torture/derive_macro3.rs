#[derive(Clone)]
struct S(i32, i32);

fn gccrs_main() -> i32 {
    let a = S(15, 15);
    let b = a.clone();

    b.0 - b.1
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
