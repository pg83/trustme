

fn gccrs_main() -> i32 {
    unsafe {
        // 1i64 = 0x0000000000000001: 63 leading zeros
        if (1i64).leading_zeros() != 63 {
            std::process::abort();
        }
        // -1i64 = 0xFFFFFFFFFFFFFFFF: 0 leading zeros
        if (-1i64).leading_zeros() != 0 {
            std::process::abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
