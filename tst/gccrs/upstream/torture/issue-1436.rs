fn gccrs_main() -> i32 {
    let array = [1, 2, 3, 4, 5];
    array[1] - 2
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
