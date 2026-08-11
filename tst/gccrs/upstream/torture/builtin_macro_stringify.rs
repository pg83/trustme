macro_rules! a {
    () => {
        "foo"
    };
}

fn gccrs_main() -> i32 {
    let tokens = stringify!(a!());
    assert_eq!(tokens, "a!()");
    let _ = a!();
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
