

fn gccrs_main() -> i32 {
    unsafe {
        if (1u8).trailing_zeros() != 0 {
            std::process::abort();
        }
        if (0xFFu8).trailing_zeros() != 0 {
            std::process::abort();
        }

        if (1u16).trailing_zeros() != 0 {
            std::process::abort();
        }
        if (0xFFFFu16).trailing_zeros() != 0 {
            std::process::abort();
        }

        if (1u32).trailing_zeros() != 0 {
            std::process::abort();
        }
        if (0xFFFFFFFFu32).trailing_zeros() != 0 {
            std::process::abort();
        }

        if (1u64).trailing_zeros() != 0 {
            std::process::abort();
        }
        if (!0u64).trailing_zeros() != 0 {
            std::process::abort();
        }

        if (1i8).trailing_zeros() != 0 {
            std::process::abort();
        }
        if (-1i8).trailing_zeros() != 0 {
            std::process::abort();
        }

        if (1i16).trailing_zeros() != 0 {
            std::process::abort();
        }
        if (-1i16).trailing_zeros() != 0 {
            std::process::abort();
        }

        if (1i32).trailing_zeros() != 0 {
            std::process::abort();
        }
        if (-1i32).trailing_zeros() != 0 {
            std::process::abort();
        }

        if (1i64).trailing_zeros() != 0 {
            std::process::abort();
        }
        if (-1i64).trailing_zeros() != 0 {
            std::process::abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
