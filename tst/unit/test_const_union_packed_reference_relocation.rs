#[derive(Clone, Copy)]
#[repr(C, packed)]
struct Packed { tag: u8, reference: &'static u16 }
union Value { packed: Packed, bytes: [u8; 9] }
static DATA: u16 = 1027;
const VALUE: Value = Value { packed: Packed { tag: 7, reference: &DATA } };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe {
        let packed = value.packed;
        assert_eq!(packed.tag, 7);
        let reference = packed.reference;
        assert_eq!(*reference, 1027);
    }
}
