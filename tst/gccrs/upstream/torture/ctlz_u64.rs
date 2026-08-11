#![feature(intrinsics)]
#![feature(lang_items)]

extern "rust-intrinsic" {
    pub fn ctlz<T>(x: T) -> T;
    pub fn abort() -> !;
}

fn gccrs_main() -> i32 {
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
