// { dg-output "t1sz=5 t2sz=10\r*" }

fn gccrs_main() -> i32 {
    let first = "TEST1";
    let second = "TEST_12345";
    println!("t1sz={} t2sz={}", first.len(), second.len());
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
