#[repr(C, align(32))]
union Value { bytes: [u8; 17], short: u8 }
const VALUE: Value = Value { bytes: [13; 17] };

fn main() {
    let value = std::hint::black_box(VALUE);
    assert_eq!(std::mem::align_of::<Value>(), 32);
    assert_eq!((&value as *const Value as usize) % 32, 0);
    unsafe { assert_eq!(value.bytes, [13; 17]); }
}
