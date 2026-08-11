// { dg-output "slice_access=3\r*\n" }

fn gccrs_main() -> i32 {
    let array = [1, 2, 3, 4, 5];
    let slice = &array[1..3];
    println!("slice_access={}", slice[1]);
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
