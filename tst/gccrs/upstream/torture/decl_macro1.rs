
#![feature(decl_macro)]
macro one() {
    1
}

fn gccrs_main() -> i32 {
    one!() - 1
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
