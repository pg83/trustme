#[repr(C, align(16))]
enum Wide { Byte(u8), Word { value: u32 }, Empty }
#[repr(C, u8, align(16))]
enum SmallTag { Byte(u8), Word(u32) }
fn main() {
    assert_eq!(std::mem::size_of::<Wide>(), 16);
    assert_eq!(std::mem::align_of::<Wide>(), 16);
    assert_eq!(std::mem::size_of::<SmallTag>(), 16);
    let values = [Wide::Byte(5), Wide::Word { value: 17 }, Wide::Empty];
    let mut total = 0;
    for value in &values {
        let address = value as *const Wide as usize;
        assert_eq!(address % 16, 0);
        total += match value {
            Wide::Byte(n) => { assert_eq!(n as *const u8 as usize - address, 4); *n as u32 }
            Wide::Word { value: n } => { assert_eq!(n as *const u32 as usize - address, 4); *n }
            Wide::Empty => 0,
        };
    }
    assert_eq!(total, 22);
    let small = SmallTag::Word(29);
    if let SmallTag::Word(n) = &small {
        assert_eq!(n as *const u32 as usize - &small as *const SmallTag as usize, 4);
        assert_eq!(*n, 29);
    } else { panic!("wrong variant"); }
    assert!(matches!(SmallTag::Byte(3), SmallTag::Byte(3)));
}
