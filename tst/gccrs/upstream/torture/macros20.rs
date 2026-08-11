
macro_rules! add {
    ($e:expr , $($es:expr) , *) => {
        $e + add!($($es) , *)
    };
    ($e:expr) => {
        $e
    };
}

fn gccrs_main() -> i32 {
    let a = add!(15, 2, 9); // 26

    a - 26
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
