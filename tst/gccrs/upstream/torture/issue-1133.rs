fn gccrs_main() -> i32 {
    let array = [1, 2, 3, 4, 5];
    let slice = &array[1..3];
    assert_eq!(slice[1], 3);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
