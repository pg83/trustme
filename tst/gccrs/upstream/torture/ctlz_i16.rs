#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn ctlz<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
    if ctlz(0i16) != 16 {
        abort();
    }
    // 1i16 = 0x0001: 15 leading zeros
    if ctlz(1i16) != 15 {
        abort();
    }
    // -1i16 = 0xFFFF: 0 leading zeros
    if ctlz(-1i16) != 0 {
        abort();
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
