#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn ctlz<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
    if ctlz(0u8) != 8 {
        abort();
    }
    if ctlz(1u8) != 7 {
        abort();
    }
    if ctlz(255u8) != 0 {
        abort();
    }

    if ctlz(0u16) != 16 {
        abort();
    }
    if ctlz(1u16) != 15 {
        abort();
    }
    if ctlz(0xFFFFu16) != 0 {
        abort();
    }

    if ctlz(0u32) != 32 {
        abort();
    }
    if ctlz(1u32) != 31 {
        abort();
    }
    if ctlz(0xFFFFFFFFu32) != 0 {
        abort();
    }

    if ctlz(0u64) != 64 {
        abort();
    }
    if ctlz(1u64) != 63 {
        abort();
    }
    if ctlz(!0u64) != 0 {
        abort();
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
