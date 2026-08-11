// { dg-output "a! ()" }

#![feature(rustc_attrs)]

macro_rules! a {
    () => {
        " foo"
    };
}

extern "C" {
    fn printf(fmt: *const i8, ...);
}

fn print(s: &str) {
    print!("{}", s);
}

fn gccrs_main() -> i32 {
    let a = stringify!(a!());

    print(a);

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
