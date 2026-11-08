//@ compile-fail: read-only
static BYTES: [u8; 3] = [11, 13, 17];
const BAD: () = unsafe {
    std::ptr::write_bytes(std::ptr::addr_of!(BYTES) as *mut u8, 0, 1);
};
fn main() { let _ = BAD; }
