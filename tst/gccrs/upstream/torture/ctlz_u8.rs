

fn gccrs_main() -> i32 {
    if (0u8).leading_zeros() != 8 {
        std::process::abort();
    }
    if (1u8).leading_zeros() != 7 {
        std::process::abort();
    }
    if (255u8).leading_zeros() != 0 {
        std::process::abort();
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
