//@ compile-fail: read-only
const BAD: () = unsafe {
    std::ptr::write_bytes(b"immutable".as_ptr() as *mut u8, 0, 1);
};
fn main() { let _ = BAD; }
