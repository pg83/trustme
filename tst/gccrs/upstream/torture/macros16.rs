
macro_rules! add {
    ($e:literal) => {
        0 + $e
    };
    ($e:literal $($es:literal)*) => {
        $e + add!($($es)*)
    };
}

fn gccrs_main() -> i32 {
    let a = add!(1 2 3 10); // 16

    a - 16
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
