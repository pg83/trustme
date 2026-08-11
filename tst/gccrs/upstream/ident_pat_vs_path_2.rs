// { dg-additional-options "-w" }

enum E {
    A,
    B,
    C
}

fn gccrs_main() -> i32 {
    use E::*;

    match A {
        C => 1,
        _ => 0
    }
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
