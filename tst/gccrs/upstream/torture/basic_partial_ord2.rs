/* { dg-output "<><=>=\r*" } */
#[derive(PartialEq, Eq, PartialOrd, Ord)] struct Bar { a: i32, b: i32 }
fn gccrs_main() -> i32 {
    let a = Bar { a: 1, b: 2 }; let b = Bar { a: 1, b: 3 }; let c = Bar { a: 1, b: 2 };
    if a < b { print!("<"); } if b > a { print!(">"); } if a <= c { print!("<="); } if b >= c { print!(">="); }
    0
}
fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
