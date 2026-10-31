extern "C" { fn snprintf(buffer: *mut u8, length: usize, format: *const u8, ...) -> i32; }
fn main() {
    let mut buffer = [0_u8; 32];
    let length = unsafe {
        snprintf(buffer.as_mut_ptr(), buffer.len(), b"%d:%.1f\0".as_ptr(), 37_i32, 2.5_f64)
    };
    assert_eq!(length, 6);
    assert_eq!(&buffer[..7], b"37:2.5\0");
}
