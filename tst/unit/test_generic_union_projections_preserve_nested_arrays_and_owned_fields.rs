//@ compile-fail: ManuallyDrop
use std::mem::ManuallyDrop;
union Slot<T> { value: ManuallyDrop<T>, empty: () }
#[repr(C)]
union Bits { word: u64, bytes: [u8; 8] }
const LOW: u8 = unsafe { Bits { bytes: [19, 0, 0, 0, 0, 0, 0, 0] }.bytes[0] };
fn take<T>(mut slot: Slot<T>) -> T { unsafe { ManuallyDrop::take(&mut slot.value) } }
fn first<T: Copy>(slot: &Slot<[T; 3]>) -> T { unsafe { slot.value[0] } }
fn main() {
    let mut array = Slot { value: ManuallyDrop::new([23u16, 29, 31]) };
    assert_eq!(first(&array), 23);
    unsafe { array.value[1] = 37; }
    assert_eq!(take(array), [23, 37, 31]);
    let owned = Slot { value: ManuallyDrop::new(Box::new(String::from("union"))) };
    assert_eq!(&**take(owned), "union");
    let mut bits = Bits { bytes: [0; 8] };
    unsafe { bits.bytes[4] = LOW; assert_eq!(bits.bytes[4], 19); }
}
