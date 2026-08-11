
fn gccrs_main() -> i32 {
    [55, 66, 77][1] - 66
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
