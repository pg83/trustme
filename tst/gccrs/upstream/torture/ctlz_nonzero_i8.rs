#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn ctlz_nonzero<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
    unsafe {
        // 1i8 = 0x01: 7 leading zeros
        if ctlz_nonzero(1i8) != 7 {
            abort();
        }
        // -1i8 = 0xFF: 0 leading zeros
        if ctlz_nonzero(-1i8) != 0 {
            abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
