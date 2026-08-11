
fn gccrs_main() -> i32 {
    let ret = {
        1;
        2;
        0
    };
    ret
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
