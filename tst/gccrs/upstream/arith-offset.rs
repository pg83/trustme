fn gccrs_main() -> i32 {
    let ptr = std::ptr::null::<u64>();
    let wrapped = ptr.wrapping_offset(-1);
    i32::from(wrapped.addr() as isize != -8)
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
