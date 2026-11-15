//@ crate-type: lib
//@ output-contains: .cpp u8 _trustme_size[536870912];

#[repr(align(536870912))]
pub union AlignedByte {
    pub byte: u8,
}

pub fn read_byte(value: &AlignedByte) -> u8 {
    unsafe { value.byte }
}
