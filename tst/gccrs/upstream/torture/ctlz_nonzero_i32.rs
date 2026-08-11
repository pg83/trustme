

fn gccrs_main() -> i32 {
    unsafe {
        // 1i32 = 0x00000001: 31 leading zeros
        if (1i32).leading_zeros() != 31 {
            std::process::abort();
        }
        // -1i32 = 0xFFFFFFFF: 0 leading zeros
        if (-1i32).leading_zeros() != 0 {
            std::process::abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
