// { dg-output "hello, include!\r*\nhello, include!\r*\nhello, include!\r*\n" }

#![feature(rustc_attrs)]

macro_rules! my_file {
    () => {"include.txt"};
}

extern "C" {
    fn printf(fmt: *const i8, ...);
}

fn print(s: &str) {
    print!("{}", s);
}

fn gccrs_main() -> i32 {
    // include_str! (and include_bytes!) allow for an optional trailing comma.
    let my_str = include_str!("include.txt",);
    print(my_str);
    let my_str = include_str!(my_file!());
    print(my_str);
    let my_str = include_str!(my_file!(),);
    print(my_str);

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
