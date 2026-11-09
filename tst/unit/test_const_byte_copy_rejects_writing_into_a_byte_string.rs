//@ compile-fail: error
const BAD: () = unsafe {
    let source = [31u8, 37];
    std::ptr::copy_nonoverlapping(
        source.as_ptr(),
        b"frozen".as_ptr() as *mut u8,
        source.len(),
    );
};

fn main() {
    let _ = BAD;
}
