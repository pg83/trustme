

fn gccrs_main() -> i32 {
    // 0i8 has all 8 bits zero
    if (0i8).leading_zeros() != 8 {
        std::process::abort();
    }
    // 1i8 = 0x01: 7 leading zeros
    if (1i8).leading_zeros() != 7 {
        std::process::abort();
    }
    // -1i8 = 0xFF in two's complement: all bits set, 0 leading zeros
    if (-1i8).leading_zeros() != 0 {
        std::process::abort();
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
