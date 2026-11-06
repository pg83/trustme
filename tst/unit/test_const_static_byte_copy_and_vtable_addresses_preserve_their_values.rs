static BYTES: [u8; 4] = [2, 3, 5, 7];
const COPIED: [u8; 4] = {
    let mut output = [0u8; 4];
    unsafe {
        std::ptr::copy_nonoverlapping(
            std::ptr::addr_of!(BYTES) as *const u8,
            output.as_mut_ptr(),
            4,
        );
    }
    output
};
trait Mark { fn value(&self) -> u8; }
struct Item(u8);
impl Mark for Item { fn value(&self) -> u8 { self.0 } }
static ITEM: Item = Item(43);
const OBJECT: &dyn Mark = &ITEM;
const VTABLE: *const () = unsafe {
    std::mem::transmute::<&dyn Mark, (*const (), *const ())>(OBJECT).1
};
fn main() {
    assert_eq!(COPIED, [2, 3, 5, 7]);
    assert_eq!(OBJECT.value(), 43);
    assert!(!std::hint::black_box(VTABLE).is_null());
}
