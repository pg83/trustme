
fn gccrs_main() -> i32 {
    let x = -55;

    match x {
        55 => 1,
        -55 => 0, // correct case
        _ => 1
    }
}
fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
