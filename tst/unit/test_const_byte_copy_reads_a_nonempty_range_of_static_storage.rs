static SOURCE: [u8; 5] = [3, 5, 8, 13, 21];
const COPIED: [u8; 3] = {
    let mut output = [0u8; 3];
    unsafe {
        std::ptr::copy_nonoverlapping(SOURCE.as_ptr().add(1), output.as_mut_ptr(), 3);
    }
    output
};
fn main() { assert_eq!(COPIED, [5, 8, 13]); }
