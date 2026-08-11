

fn gccrs_main() -> i32 {
    if (0u64).leading_zeros() != 64 {
        std::process::abort();
    }
    if (1u64).leading_zeros() != 63 {
        std::process::abort();
    }
    if (!0u64).leading_zeros() != 0 {
        std::process::abort();
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
