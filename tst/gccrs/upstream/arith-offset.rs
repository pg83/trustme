#![feature(intrinsics, lang_items)]

extern "rust-intrinsic" {
    fn arith_offset<T>(dst: *const T, offset: isize) -> *const T;
}

fn gccrs_main() -> i32 {
    let base_addr: usize = 0;
    let ptr = base_addr as *const u64;

    unsafe {
        let wrap_ptr = arith_offset(ptr, -1);
        if wrap_ptr as isize == -8_isize {
            0 
        } else {
            1 
        }
    }
}

fn main() { let code = gccrs_main() as i32; if code != 0 { std::process::exit(code); } }
