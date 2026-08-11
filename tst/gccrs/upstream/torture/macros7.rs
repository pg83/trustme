// { dg-output "any\r*\nany\r*\nany\r*\n" }

extern "C" {
    fn printf(s: *const i8, ...);
}

fn f() {
    let r_s = "any\n\0";
    let s_p = r_s as *const str;
    let c_p = s_p as *const i8;

    unsafe {
        printf(c_p);
    }
}

macro_rules! any {
    ($($a:expr)*) => {
        f();
    };
}

fn gccrs_main() -> i32 {
    any!();
    any!(a + b);
    any!(a + b    14 "gcc");

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
