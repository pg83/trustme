// { dg-output "1\r*\n2\r*\n" }

fn gccrs_main() -> i32 {
    let mut iterator = (1..3).into_iter();
    while let Some(value) = iterator.next() {
        println!("{}", value);
    }
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
