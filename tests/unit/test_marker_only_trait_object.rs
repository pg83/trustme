use std::mem::ManuallyDrop;

#[repr(C, packed(2))]
struct Packed<T: ?Sized>(u8, ManuallyDrop<T>);

fn main() {
    assert!(!std::mem::needs_drop::<Packed<dyn Send>>());
}
