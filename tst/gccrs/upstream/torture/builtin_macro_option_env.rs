// { dg-output "VALUE\r*\nVALUE\r*\n" }
// { dg-set-compiler-env-var ENV_MACRO_TEST "VALUE" }


use std::option::Option;

extern "C" {
    fn printf(fmt: *const i8, ...);
}

fn print(s: &str) {
    println!("{}", s);
}

macro_rules! env_macro_test {
    () => { "ENV_MACRO_TEST" }
}

fn gccrs_main() -> i32 {
    let val0: Option<&'static str> = option_env!("ENV_MACRO_TEST");

    
    match val0 {
        Option::None => {},
        Option::Some(s) => {
            print(s);
        }
    }

    //eager expansion test
    let val1: Option<&'static str> = option_env!(env_macro_test!(),);

    match val1 {
        Option::None => {},
        Option::Some(s) => {
            print(s);
        }
    }
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
