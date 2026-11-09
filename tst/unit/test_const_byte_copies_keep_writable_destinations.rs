static SOURCE: [u8; 3] = [61, 67, 71];

const OUTPUT: [u8; 6] = {
    let mut output = [0u8; 6];
    unsafe {
        std::ptr::copy_nonoverlapping(b"ok".as_ptr(), output.as_mut_ptr(), 2);
        std::ptr::copy_nonoverlapping(
            std::ptr::addr_of!(SOURCE) as *const u8,
            output.as_mut_ptr().add(2),
            3,
        );
        std::ptr::copy(output.as_ptr().add(2), output.as_mut_ptr().add(3), 3);
    }
    output
};

fn main() {
    assert_eq!(OUTPUT, [111, 107, 61, 61, 67, 71]);
}
