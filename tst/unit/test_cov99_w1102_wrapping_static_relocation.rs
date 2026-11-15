struct SavedPointer(*const u8);

unsafe impl Sync for SavedPointer {}

static BYTE: u8 = 73;
static SHIFTED: SavedPointer = SavedPointer((&BYTE as *const u8).wrapping_sub(0x1000));

fn main() {
    let saved = unsafe { std::ptr::read_volatile(&SHIFTED.0) };
    let restored = saved.wrapping_add(0x1000);
    assert!(restored == &BYTE as *const u8);
    assert!(unsafe { *restored } == 73);
}
