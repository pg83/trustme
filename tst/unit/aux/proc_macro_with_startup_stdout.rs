extern crate proc_macro;

extern "C" {
    fn write(fd: i32, bytes: *const u8, length: usize) -> isize;
}

extern "C" fn write_before_macro_dispatch() {
    unsafe {
        write(1, b"!".as_ptr(), 1);
    }
}

#[used]
#[link_section = ".init_array"]
static START: extern "C" fn() = write_before_macro_dispatch;

#[proc_macro_attribute]
pub fn preserve(_: proc_macro::TokenStream, item: proc_macro::TokenStream) -> proc_macro::TokenStream {
    item
}
