// { dg-output "zo1\r*\nzo1\r*\n" }

extern "C" {
    fn printf(s: *const i8, ...);
}

fn f() {
    let r_s = "zo1\n\0";
    let s_p = r_s as *const str;
    let c_p = s_p as *const i8;

    unsafe {
        printf(c_p);
    }
}

macro_rules! zero_or_one {
    ($($a:expr)?) => {
        f();
    };
}

fn gccrs_main() -> i32 {
    zero_or_one!();
    zero_or_one!(f());

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
