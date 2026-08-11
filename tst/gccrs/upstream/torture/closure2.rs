// { dg-output "3\r*\n" }

#![feature(lang_items)]

extern "C" {
    fn printf(s: *const i8, ...);
}

fn f<F: FnOnce(i32) -> i32>(g: F) {
    let call = g(1);
    unsafe {
        let a = "%i\n\0";
        let b = a as *const str;
        let c = b as *const i8;

        printf(c, call);
    }
}

pub fn gccrs_main() -> i32 {
    let a = |i: i32| {
        let b = i + 2;
        b
    };
    f(a);
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
