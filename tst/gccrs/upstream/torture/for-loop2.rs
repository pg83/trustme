// { dg-output "loop1\r*\nloop2\r*\n" }

fn gccrs_main() -> i32 {
    for i in 1usize..3usize {
        println!("loop{}", i);
    }
    0
}

fn main() { let code = gccrs_main(); if code != 0 { std::process::exit(code); } }
