extern crate proc_macro;

extern "C" {
    fn write(fd: i32, bytes: *const u8, count: usize) -> isize;
}

extern "C" fn bad_greeting() {
    unsafe {
        write(1, b"\x01".as_ptr(), 1);
    }
}

#[used]
#[link_section = ".init_array"]
static STARTUP: extern "C" fn() = bad_greeting;

#[proc_macro_attribute]
pub fn reject(_: proc_macro::TokenStream, _: proc_macro::TokenStream) -> proc_macro::TokenStream {
    panic!("w436 intentional proc macro rejection")
}
