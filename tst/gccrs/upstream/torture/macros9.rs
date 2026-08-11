// { dg-output "oom\r*\noom\r*\noom\r*\n" }

extern "C" {
    fn printf(s: *const i8, ...);
}

fn f() {
    let r_s = "oom\n\0";
    let s_p = r_s as *const str;
    let c_p = s_p as *const i8;

    unsafe {
        printf(c_p);
    }
}

macro_rules! one_or_more {
    ($($a:expr)+) => {
        f();
    };
}

fn gccrs_main() -> i32 {
    one_or_more!(f());
    one_or_more!(f() f());
    one_or_more!(f() f() 15 + 12);

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
