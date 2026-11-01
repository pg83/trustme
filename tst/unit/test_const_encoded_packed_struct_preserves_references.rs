#[repr(C, packed)]
struct Values { tag: u8, left: &'static u16, right: &'static u64, tail: u8 }
static LEFT: u16 = 257;
static RIGHT: u64 = 0x1234_5678_9abc_def0;
const VALUE: Values = Values { tag: 3, left: &LEFT, right: &RIGHT, tail: 7 };

fn main() {
    let value = std::hint::black_box(VALUE);
    assert_eq!((value.tag, value.tail), (3, 7));
    let left = value.left;
    let right = value.right;
    assert_eq!((*left, *right), (257, 0x1234_5678_9abc_def0));
}
