
#![feature(lang_items)]
macro_rules! t {
    () => {
        i32
    };
}

fn id<T>(arg: T) -> T {
    arg
}

fn gccrs_main() -> i32 {
    id::<t!()>(15) - 15
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
