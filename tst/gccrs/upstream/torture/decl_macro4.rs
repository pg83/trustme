
#![feature(decl_macro)]
pub macro add {
    ($e:expr) => {
        $e
    },
    ($h:expr, $($t:expr),*) => {
        $h + add!($($t),*)
    },
}

fn gccrs_main() -> i32 {
    let a = add!(1, 2, 3);

    a - 6
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
