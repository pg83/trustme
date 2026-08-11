// { dg-output "14\r*\n42\r*\n" }

#![feature(rustc_attrs)]

extern "C" {
    fn printf(fmt: *const i8, ...);
}

fn print(s: u32) {
    unsafe {
        printf("%u\n\0" as *const str as *const i8, s);
    }
}

fn gccrs_main() -> i32 {
    let c0 = column!();

    print(c0);

    let c1 = /* --------------------- */ column!();

    print(c1);

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
