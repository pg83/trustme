

fn gccrs_main() -> i32 {
    unsafe {
        // 1i16 = 0x0001: 15 leading zeros
        if (1i16).leading_zeros() != 15 {
            std::process::abort();
        }
        // -1i16 = 0xFFFF: 0 leading zeros
        if (-1i16).leading_zeros() != 0 {
            std::process::abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
