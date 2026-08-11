fn gccrs_main() -> i32 {
    let _value = std::mem::MaybeUninit::<usize>::uninit();
    0
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
