union Reference { word: &'static u16, byte: &'static u8 }
#[repr(C)]
struct Tagged { tag: u8, value: Reference }
static DATA: u8 = 47;
const VALUE: Tagged = Tagged { tag: 9, value: Reference { byte: &DATA } };

fn main() {
    let value = std::hint::black_box(VALUE);
    assert_eq!(value.tag, 9);
    unsafe { assert_eq!(*value.value.byte, 47); }
}
