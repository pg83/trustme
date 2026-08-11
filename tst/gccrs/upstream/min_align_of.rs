// { dg-require-effective-target lp64 }

fn gccrs_main() -> i32 {
    i32::from(std::mem::align_of::<u16>() != 2 || std::mem::align_of::<i32>() != 4)
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
