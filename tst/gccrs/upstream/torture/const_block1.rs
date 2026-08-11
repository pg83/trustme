
const X: i32 = const {
    let a = 15;
    let b = 14;
    a + b
};

fn gccrs_main() -> i32 {
    X - 29
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
