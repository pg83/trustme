
#![feature(intrinsics)]

#![feature(lang_items)]
mod intrinsics {
    extern "rust-intrinsic" {
        pub fn uninit<T>() -> T;
    }
}

pub fn gccrs_main() -> i32 {
    let _val: usize = unsafe { intrinsics::uninit() };
    0
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
