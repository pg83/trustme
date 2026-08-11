
fn gccrs_main() -> i32 {
    let (x, y, z) = (2, 3, 6);
    x * y - z
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
