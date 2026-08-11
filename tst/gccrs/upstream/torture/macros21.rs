
macro_rules! add_parens {
    ($($rep:ident ( ) )*) => {
        { 0 $(+ $rep ( ))* }
    };
}

fn f() -> i32 {
    1
}

fn gccrs_main() -> i32 {
    let a = add_parens!(f() f() f());

    a - 3
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
