
macro_rules! foo {
    () => {
        $crate::bar()
    }
}

pub fn bar() -> i32 { 1 }

fn gccrs_main() -> i32 {
    foo!() - crate::bar()
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
