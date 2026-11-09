//@ compile-fail: error
static DESTINATION: [u8; 3] = [41, 43, 47];

const BAD: () = unsafe {
    let source = [53u8, 59];
    std::ptr::copy_nonoverlapping(
        source.as_ptr(),
        std::ptr::addr_of!(DESTINATION) as *mut u8,
        source.len(),
    );
};

fn main() {
    let _ = BAD;
}
