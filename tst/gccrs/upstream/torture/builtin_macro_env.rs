// { dg-output "VALUE\r*\nVALUE\r*\n" }
// { dg-set-compiler-env-var ENV_MACRO_TEST "VALUE" }

#![feature(rustc_attrs)]

extern "C" {
    fn printf(fmt: *const i8, ...);
}

fn print(s: &str) {
    println!("{}", s);
}

fn gccrs_main() -> i32 {
    let val0 = env!("ENV_MACRO_TEST");

    print(val0);

    let val1 = env!("ENV_MACRO_TEST",);

    print(val1);

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
