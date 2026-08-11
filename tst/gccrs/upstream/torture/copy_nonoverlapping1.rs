fn gccrs_main() -> i32 {
    let source = 15;
    let mut destination = 16;
    unsafe {
        std::ptr::copy_nonoverlapping(&source, &mut destination, 1);
    }
    destination - source
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
