// { dg-require-effective-target lp64 }
#![feature(intrinsics, lang_items)]

extern "rust-intrinsic" {
    pub fn min_align_of<T>() -> usize;
}

fn gccrs_main() -> i32 {
    let align_u16 = get_u16_align() as i32;
    let align_u32 = get_i32_align() as i32;

    if align_u16 != 2 || align_u32 != 4 {
        1
    } else {
        0
    }
}

pub fn get_u16_align() -> usize {
    unsafe { min_align_of::<u16>() }
}

pub fn get_i32_align() -> usize {
    unsafe { min_align_of::<i32>() }
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
