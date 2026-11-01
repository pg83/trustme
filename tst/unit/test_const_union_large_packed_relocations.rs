#[derive(Clone, Copy)]
#[repr(C, packed)]
struct Packed { left_tag: u8, left: &'static u16, right_tag: u8, right: &'static u32, tail: u8 }
union Value { packed: Packed, bytes: [u8; 19] }
static LEFT: u16 = 259;
static RIGHT: u32 = 0x1234_5678;
const VALUE: Value = Value { packed: Packed { left_tag: 3, left: &LEFT, right_tag: 5, right: &RIGHT, tail: 7 } };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe {
        let packed = value.packed;
        assert_eq!((packed.left_tag, packed.right_tag, packed.tail), (3, 5, 7));
        let left = packed.left;
        let right = packed.right;
        assert_eq!((*left, *right), (259, 0x1234_5678));
    }
}
