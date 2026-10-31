static VALUE: u32 = 37;
static PAIR: (u32, u32) = (17, 23);
static BYTES: [u8; 4] = [3, 5, 7, 11];
const ADDRESS: &u32 = &VALUE;
const OFFSET: &u8 = &BYTES[2];
fn main() {
    let pointer = &VALUE;
    let copied = pointer;
    let first = *pointer;
    let second = *copied;
    let pair = &PAIR;
    let sum = pair.0 + pair.1;
    let constant = ADDRESS;
    let offset = OFFSET;
    assert_eq!((first, second, sum, *constant, *offset), (37, 37, 40, 37, 7));
    assert!(std::ptr::eq(copied, &VALUE));
}
