

fn gccrs_main() -> i32 {
    unsafe {
        if (1u32).leading_zeros() != 31 {
            std::process::abort();
        }
        if (0xFFFFFFFFu32).leading_zeros() != 0 {
            std::process::abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
