//@ compile-flags: -O

trait Byte {
    fn byte(&self) -> u8;
}

impl Byte for u8 {
    fn byte(&self) -> u8 {
        *self
    }
}

#[inline(never)]
fn virtual_choice(value: &dyn Byte, flag: bool) -> u8 {
    if flag {
        value.byte()
    } else {
        value.byte()
    }
}

#[inline(never)]
fn data_choice(source: *const [u8], flag: bool) -> *const u8 {
    let result;
    if flag {
        result = source as *const u8;
    } else {
        result = source as *const u8;
    }
    result
}

fn main() {
    let bytes = [73u8, 29];
    let slice: &[u8] = &bytes;
    let source = slice as *const [u8];
    assert_eq!(data_choice(source, true), bytes.as_ptr());
    assert_eq!(data_choice(source, false), bytes.as_ptr());
    assert_eq!(virtual_choice(&bytes[0], true), 73);
    assert_eq!(virtual_choice(&bytes[0], false), 73);
}
