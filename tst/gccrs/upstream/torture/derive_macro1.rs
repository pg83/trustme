
#[derive(Clone, Copy)]
struct Number(i32);

fn gccrs_main() -> i32 {
    let a = Number(15);
    let b = a.clone();

    a.0 - b.0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
