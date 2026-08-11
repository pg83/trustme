#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn ctlz<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
    if ctlz(0i32) != 32 {
        abort();
    }
    // 1i32 = 0x00000001: 31 leading zeros
    if ctlz(1i32) != 31 {
        abort();
    }
    // -1i32 = 0xFFFFFFFF: 0 leading zeros
    if ctlz(-1i32) != 0 {
        abort();
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
