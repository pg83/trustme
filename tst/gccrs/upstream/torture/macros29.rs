// { dg-output "1\r*\n" }

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
    let res = concat!("test2") == "test3";
    if !res {
        print(1);
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
