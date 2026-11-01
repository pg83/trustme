union Reference { word: &'static u16, byte: &'static u8 }
static DATA: u8 = 37;
const VALUE: Reference = Reference { byte: &DATA };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe {
        assert_eq!(value.byte, &DATA);
        assert_eq!(*value.byte, 37);
    }
}
