//@ compile-variant: -O
#![feature(const_trait_impl)]
trait Value { fn value(&self) -> u32; }
impl Value for u32 { fn value(&self) -> u32 { *self } }
struct Packet<T: ?Sized> { tag: u16, payload: T }
const DATA: [u32; 3] = [7, 13, 23];
const PACKET: Packet<[u32; 3]> = Packet { tag: 31, payload: DATA };
const SLICE: *const [u32] = &DATA as *const [u32; 3] as *const [u32];
const OBJECT: *const dyn Value = &DATA[1] as *const u32 as *const dyn Value;
const UNSIZED: *const Packet<[u32]> = &PACKET as *const Packet<[u32; 3]> as *const Packet<[u32]>;
const fn length(pointer: *const [u32]) -> usize { pointer.len() }
const LENGTH: usize = length(SLICE);
fn main() {
    assert_eq!(LENGTH, 3);
    unsafe {
        assert_eq!(&*SLICE, &DATA);
        assert_eq!((*OBJECT).value(), 13);
        assert_eq!((*UNSIZED).tag, 31);
        assert_eq!(&(*UNSIZED).payload, &DATA);
    }
}
