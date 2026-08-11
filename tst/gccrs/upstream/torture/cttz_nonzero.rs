#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn cttz_nonzero<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
    unsafe {
        if cttz_nonzero(1u8) != 0 {
            abort();
        }
        if cttz_nonzero(0xFFu8) != 0 {
            abort();
        }

        if cttz_nonzero(1u16) != 0 {
            abort();
        }
        if cttz_nonzero(0xFFFFu16) != 0 {
            abort();
        }

        if cttz_nonzero(1u32) != 0 {
            abort();
        }
        if cttz_nonzero(0xFFFFFFFFu32) != 0 {
            abort();
        }

        if cttz_nonzero(1u64) != 0 {
            abort();
        }
        if cttz_nonzero(!0u64) != 0 {
            abort();
        }

        if cttz_nonzero(1i8) != 0 {
            abort();
        }
        if cttz_nonzero(-1i8) != 0 {
            abort();
        }

        if cttz_nonzero(1i16) != 0 {
            abort();
        }
        if cttz_nonzero(-1i16) != 0 {
            abort();
        }

        if cttz_nonzero(1i32) != 0 {
            abort();
        }
        if cttz_nonzero(-1i32) != 0 {
            abort();
        }

        if cttz_nonzero(1i64) != 0 {
            abort();
        }
        if cttz_nonzero(-1i64) != 0 {
            abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
