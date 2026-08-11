// { dg-xfail-run-if "" { *-*-* } }


fn gccrs_main() -> i32 {
    1
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
