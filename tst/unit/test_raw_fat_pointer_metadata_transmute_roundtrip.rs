trait Read { fn read(&self) -> u32; }
impl Read for u32 { fn read(&self) -> u32 { *self } }
#[inline(never)]
unsafe fn as_slice(pointer: *const dyn Read) -> *const [u8] {
    std::mem::transmute(pointer)
}
#[inline(never)]
unsafe fn as_object(pointer: *const [u8]) -> *const dyn Read {
    std::mem::transmute(pointer)
}
fn main() {
    let value = 37u32;
    let original: *const dyn Read = &value;
    let restored = unsafe { as_object(as_slice(original)) };
    assert!(std::ptr::eq(original, restored));
    assert_eq!(unsafe { (*restored).read() }, 37);
}
