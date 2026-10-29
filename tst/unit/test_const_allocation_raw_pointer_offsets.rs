const START: *const u8 = b"branch".as_ptr();
const OFFSET: *const u8 = unsafe { b"branch".as_ptr().add(3) };
const END: *const u8 = unsafe { b"branch".as_ptr().add(6) };
fn main() {
    unsafe {
        assert_eq!(*START, b'b');
        assert_eq!(*OFFSET, b'n');
        assert_eq!(*END.sub(1), b'h');
    }
}
