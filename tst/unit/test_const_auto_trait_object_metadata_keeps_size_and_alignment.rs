#[repr(align(32))]
struct Aligned([u16; 3]);
static DATA: Aligned = Aligned([2, 3, 5]);
const SEND: &dyn Send = &DATA;
const SYNC: &(dyn Sync + Send) = &DATA;

fn main() {
    assert_eq!(std::mem::size_of_val(SEND), 32);
    assert_eq!(std::mem::align_of_val(SEND), 32);
    assert_eq!(std::mem::size_of_val(SYNC), 32);
    assert_eq!(std::mem::align_of_val(SYNC), 32);
    assert_eq!(DATA.0[1], 3);
}
