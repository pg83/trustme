
macro_rules! add {
    ($a:expr,$b:expr) => {
        $a + $b
    };
    ($a:expr) => {
        $a
    };
}

fn gccrs_main() -> i32 {
    let mut x = add!(1);
    x += add!(2, 3);

    x - 6
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
