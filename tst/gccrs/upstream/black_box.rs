/* { dg-output "Value is: 42\r*\n" } */

#![feature(rustc_attrs, lang_items)]

extern "C" {
    fn printf(s: *const i8, ...);
}

pub fn black_box<T>(dummy: T) -> T {
    std::hint::black_box(dummy)
}

fn gccrs_main() -> i32 {
    let dummy: i32 = 42;
    let result = black_box(dummy);
    unsafe {
        printf("Value is: %i\n\0" as *const str as *const i8, result);
    }
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
