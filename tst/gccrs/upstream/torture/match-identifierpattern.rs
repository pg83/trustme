
fn gccrs_main() -> i32 {
    let mut x = 2;

    match x {
        a @ 2 => { x = a + 1 },
        _ => {}
    }

    x - 3
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
