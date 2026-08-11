

fn gccrs_main() -> i32 {
    unsafe {
        if (1u8).leading_zeros() != 7 {
            std::process::abort();
        }
        if (255u8).leading_zeros() != 0 {
            std::process::abort();
        }

        if (1u16).leading_zeros() != 15 {
            std::process::abort();
        }
        if (0xFFFFu16).leading_zeros() != 0 {
            std::process::abort();
        }

        if (1u32).leading_zeros() != 31 {
            std::process::abort();
        }
        if (0xFFFFFFFFu32).leading_zeros() != 0 {
            std::process::abort();
        }

        if (1u64).leading_zeros() != 63 {
            std::process::abort();
        }
        if (!0u64).leading_zeros() != 0 {
            std::process::abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
