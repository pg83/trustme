//@ compile-fail: read-only
const BAD: () = unsafe {
    std::ptr::copy(b"read".as_ptr(), b"only".as_ptr() as *mut u8, 4);
};

fn main() {
    let _ = BAD;
}
