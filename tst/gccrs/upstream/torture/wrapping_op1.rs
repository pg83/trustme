
#![feature(intrinsics)]

#![feature(lang_items)]
extern "rust-intrinsic" {
    pub fn wrapping_add<T>(l: T, r: T) -> T;
}

fn five() -> u8 {
    5
}

fn gccrs_main() -> u8 {
    let l = 255;
    let r = five();

    unsafe { wrapping_add(l, r) - 4 }
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
