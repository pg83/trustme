#![feature(ptr_metadata)]
#[repr(C)]
struct Packet<T: ?Sized> { tag: u32, data: T }
#[inline(never)]
fn inspect(value: &Packet<[u16]>) -> (u32, usize, u16) {
    let pointer = value as *const Packet<[u16]>;
    let rebuilt = std::ptr::from_raw_parts::<Packet<[u16]>>(pointer as *const (), std::ptr::metadata(pointer));
    unsafe {
        let value = &*rebuilt;
        (value.tag, value.data.len(), value.data[1])
    }
}
fn main() {
    let value = Packet { tag: 37, data: [41, 43, 47] };
    assert_eq!(inspect(&value), (37, 3, 43));
}
