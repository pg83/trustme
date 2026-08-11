
#![feature(exclusive_range_pattern)]

fn gccrs_main() -> i32 {
    let x = -77;

    match x {
        -55..99 => 1,
        -99..-55 => 0, // the correct case
        _ => 1,
    }
}
fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
