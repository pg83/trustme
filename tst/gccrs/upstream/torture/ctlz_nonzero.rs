#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn ctlz_nonzero<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
    unsafe {
        if ctlz_nonzero(1u8) != 7 {
            abort();
        }
        if ctlz_nonzero(255u8) != 0 {
            abort();
        }

        if ctlz_nonzero(1u16) != 15 {
            abort();
        }
        if ctlz_nonzero(0xFFFFu16) != 0 {
            abort();
        }

        if ctlz_nonzero(1u32) != 31 {
            abort();
        }
        if ctlz_nonzero(0xFFFFFFFFu32) != 0 {
            abort();
        }

        if ctlz_nonzero(1u64) != 63 {
            abort();
        }
        if ctlz_nonzero(!0u64) != 0 {
            abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
