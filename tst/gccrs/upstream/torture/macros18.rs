
macro_rules! add {
    ($e:literal) => {
        0 + $e
    };
    ($e:literal $($es:literal)*) => {
        $e + add!($($es)*)
    };
}

fn gccrs_main() -> i32 {
    let a = add!(3 4); // 7

    a - 7
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
