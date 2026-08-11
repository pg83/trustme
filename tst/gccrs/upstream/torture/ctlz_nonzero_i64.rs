#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn ctlz_nonzero<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
    unsafe {
        // 1i64 = 0x0000000000000001: 63 leading zeros
        if ctlz_nonzero(1i64) != 63 {
            abort();
        }
        // -1i64 = 0xFFFFFFFFFFFFFFFF: 0 leading zeros
        if ctlz_nonzero(-1i64) != 0 {
            abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
