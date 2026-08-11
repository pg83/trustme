#![feature(intrinsics, lang_items)]

extern "rust-intrinsic" {
    fn assert_zero_valid<T>();
}

fn main() {
    unsafe {
        assert_zero_valid::<i32>();
        assert_zero_valid::<&i32>();
    }
}
