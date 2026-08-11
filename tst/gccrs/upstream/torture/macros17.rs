
macro_rules! two {
    (2) => {
        3
    };
}

macro_rules! one {
    (1) => {{
        two!(2)
    }};
}

fn gccrs_main() -> i32 {
    let a = one!(1);

    a - 3
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
