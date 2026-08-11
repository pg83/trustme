
#[macro_use]
mod foo {
    macro_rules! a {
        () => {
            15
        };
    }

    macro_rules! b {
        () => {
            14
        };
    }
}

fn gccrs_main() -> i32 {
    a!() + b!() - 29
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
