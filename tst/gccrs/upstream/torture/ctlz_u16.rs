

fn gccrs_main() -> i32 {
    if (0u16).leading_zeros() != 16 {
        std::process::abort();
    }
    if (1u16).leading_zeros() != 15 {
        std::process::abort();
    }
    if (0xFFFFu16).leading_zeros() != 0 {
        std::process::abort();
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
