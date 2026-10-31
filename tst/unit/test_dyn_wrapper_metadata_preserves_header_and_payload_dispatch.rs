#![feature(ptr_metadata)]
trait Value { fn value(&self) -> u32; }
struct Number(u32);
impl Value for Number { fn value(&self) -> u32 { self.0 + 7 } }
#[repr(C)]
struct Packet<T: ?Sized> { tag: u32, data: T }
#[inline(never)]
fn inspect(value: &Packet<dyn Value>) -> (u32, u32) {
    let pointer = value as *const Packet<dyn Value>;
    let rebuilt = std::ptr::from_raw_parts::<Packet<dyn Value>>(pointer as *const (), std::ptr::metadata(pointer));
    unsafe { ((*rebuilt).tag, (*rebuilt).data.value()) }
}
fn main() {
    let value = Packet { tag: 37, data: Number(41) };
    assert_eq!(inspect(&value), (37, 48));
}
