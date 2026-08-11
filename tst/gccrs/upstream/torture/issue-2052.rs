
#![feature(lang_items)]
pub fn f() -> i32 {
    (|| 42)()
}

pub fn gccrs_main() -> i32 {
    f() - 42
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
