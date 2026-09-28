// libloading's helper cdylib exports `#[no_mangle] pub static mut
// TEST_STATIC_U32`, which the test looks up with dlsym. A static's symbol is
// its `#[no_mangle]` or `#[export_name]` name, as a function's is; the C
// backend labelled functions with it but defined statics under their mangled
// name only.
#[no_mangle]
pub static mut SHARED_COUNTER: u32 = 7;

#[export_name = "renamed_limit"]
pub static LIMIT: u64 = 0x1234_5678_9abc;

mod ffi {
    extern "C" {
        #[link_name = "SHARED_COUNTER"]
        pub static mut COUNTER_BY_NAME: u32;
        #[link_name = "renamed_limit"]
        pub static LIMIT_BY_NAME: u64;
    }
}

fn main() {
    unsafe {
        let counter = &raw const ffi::COUNTER_BY_NAME;
        assert_eq!(*counter, 7);
        SHARED_COUNTER = 42;
        assert_eq!(*counter, 42);
        assert_eq!(ffi::LIMIT_BY_NAME, LIMIT);
    }
}
