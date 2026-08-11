
#![feature(lang_items)]
extern "C" {
    fn printf(s: *const i8, ...);
}

fn gccrs_main() -> i32 {
    let closure_annotated = |i: i32| -> i32 { i + 1 };

    let i = 1;
    closure_annotated(i) - 2
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
