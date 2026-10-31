#![feature(ptr_metadata)]
trait Value { fn value(&self) -> u64; }
struct Small(u8);
struct Wide(u64, u64);
impl Value for Small { fn value(&self) -> u64 { self.0 as u64 } }
impl Value for Wide { fn value(&self) -> u64 { self.0 + self.1 } }
#[inline(never)]
fn inspect(value: Box<dyn Value>) -> (u64, usize, usize) {
    let pointer: *const dyn Value = &*value;
    let metadata = std::ptr::metadata(pointer);
    let rebuilt = std::ptr::from_raw_parts::<dyn Value>(pointer as *const (), metadata);
    (unsafe { (&*rebuilt).value() }, metadata.size_of(), metadata.align_of())
}
fn main() {
    assert_eq!(inspect(Box::new(Small(37))), (37, 1, 1));
    assert_eq!(inspect(Box::new(Wide(41, 43))), (84, 16, 8));
}
