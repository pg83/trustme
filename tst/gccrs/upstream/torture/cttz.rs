#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn cttz<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
    // cttz(0) must return bit_size per the Rust reference
    if cttz(0u8) != 8 {
        abort();
    }
    if cttz(1u8) != 0 {
        abort();
    }
    if cttz(0xFFu8) != 0 {
        abort();
    }

    if cttz(0u16) != 16 {
        abort();
    }
    if cttz(1u16) != 0 {
        abort();
    }
    if cttz(0xFFFFu16) != 0 {
        abort();
    }

    if cttz(0u32) != 32 {
        abort();
    }
    if cttz(1u32) != 0 {
        abort();
    }
    if cttz(0xFFFFFFFFu32) != 0 {
        abort();
    }

    if cttz(0u64) != 64 {
        abort();
    }
    if cttz(1u64) != 0 {
        abort();
    }
    if cttz(!0u64) != 0 {
        abort();
    }

    if cttz(0i8) != 8 {
        abort();
    }
    if cttz(1i8) != 0 {
        abort();
    }
    if cttz(-1i8) != 0 {
        abort();
    }

    if cttz(0i16) != 16 {
        abort();
    }
    if cttz(1i16) != 0 {
        abort();
    }
    if cttz(-1i16) != 0 {
        abort();
    }

    if cttz(0i32) != 32 {
        abort();
    }
    if cttz(1i32) != 0 {
        abort();
    }
    if cttz(-1i32) != 0 {
        abort();
    }

    if cttz(0i64) != 64 {
        abort();
    }
    if cttz(1i64) != 0 {
        abort();
    }
    if cttz(-1i64) != 0 {
        abort();
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
