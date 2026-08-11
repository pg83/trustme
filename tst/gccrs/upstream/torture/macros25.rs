
macro_rules! t {
    ($t:tt) => {
        $t
    };
}

fn frob() -> i32 {
    t!(15) + t!((14))
}

fn gccrs_main() -> i32 {
    frob() - 29
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
