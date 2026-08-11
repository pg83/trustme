fn five() -> u8 {
    5
}

fn gccrs_main() -> u8 {
    255u8.wrapping_add(five()) - 4
}

fn main() {
    let code = gccrs_main() as i32;
    if code != 0 {
        std::process::exit(code);
    }
}
