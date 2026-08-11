// { dg-output "gcc\n\nrs\n" }


extern "C" {
    fn printf(fmt: *const i8, ...);
}

fn gccrs_main() -> i32 {
    let a = "gcc

rs\0";

    unsafe { printf("%s\n\0" as *const str as *const i8, a as *const str as *const i8); }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
