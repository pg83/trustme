
#![feature(lang_items)]

fn takes_fn(a: i32, f: impl FnOnce(i32) -> i32) -> i32 {
    f(a)
}

pub fn gccrs_main() -> i32 {
    let capture = 2;
    let a = |i: i32| {
        let b = i + capture;
        b
    };
    takes_fn(1, a) - 3
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
