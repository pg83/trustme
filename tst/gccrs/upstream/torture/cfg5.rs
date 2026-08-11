// { dg-additional-options "-w -frust-cfg=A" }
fn gccrs_main() -> i32 {
    let mut a = 0;

    if cfg!(A) {
        a = 3;
    }

    if cfg!(B) {
        a = 40;
    }

    a - 3
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
