union Overlay {
    byte: u8,
    bytes: [u8; 1],
}

const COPIED: u8 = unsafe {
    let mut value = Overlay { byte: 73 };
    let src = &raw const value.byte;
    let dst = &raw mut value.bytes;
    core::ptr::copy(src, dst as *mut u8, 1);
    value.byte
};

fn main() {
    assert_eq!(COPIED, 73);
}
