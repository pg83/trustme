fn gccrs_main() -> i32 {
    let mut value = 0u32;
    unsafe {
        std::ptr::write_bytes(&mut value, 0xec, 1);
    }
    i32::from(value != 0xecececec)
}

fn main() {
    let code = gccrs_main();
    if code != 0 {
        std::process::exit(code);
    }
}
