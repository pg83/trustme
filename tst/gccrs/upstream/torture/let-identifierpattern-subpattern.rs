
fn gccrs_main() -> i32 {
    let foo @ (bar, _, _) = (0, 2, 3);
    let mut ret = 1;

    match foo {
        (0, 2, 3) => { ret = bar },
        _ => {}
    }

    ret
}
fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
