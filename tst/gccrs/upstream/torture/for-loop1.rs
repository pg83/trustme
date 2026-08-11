// { dg-output "loop\r*\nloop\r*\n" }

fn gccrs_main() -> i32 {
    for _ in 1usize..3usize {
        println!("loop");
    }
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
