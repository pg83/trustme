fn gccrs_main() -> i32 {
    let first = line!();
    let second = line!();
    assert!(first > 0);
    assert!(second > first);
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
