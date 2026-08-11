fn five() -> u8 {
    5
}

fn gccrs_main() -> u8 {
    let left = 255u8;
    let right = five();

    let add = left.wrapping_add(right) - 4;
    let sub = right.wrapping_sub(left) - 6;
    let mul = right.wrapping_mul(left) - 251;
    add + sub + mul
}

fn main() {
    let code = gccrs_main() as i32;
    if code != 0 {
        std::process::exit(code);
    }
}
