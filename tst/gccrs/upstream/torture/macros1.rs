
macro_rules! add {
    ($a:expr,$b:expr) => {
        $a + $b
    };
}

fn test() -> i32 {
    add!(1 + 2, 3)
}

fn gccrs_main() -> i32 {
    test() - 6
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
