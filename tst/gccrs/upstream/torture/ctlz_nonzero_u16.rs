#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn ctlz_nonzero<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
    unsafe {
        if ctlz_nonzero(1u16) != 15 {
            abort();
        }
        if ctlz_nonzero(0xFFFFu16) != 0 {
            abort();
        }
    }

    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
