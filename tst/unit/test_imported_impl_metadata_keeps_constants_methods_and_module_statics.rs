//@ aux-build: impl_metadata_members.rs
//@ compile-variant: -O
use impl_metadata_members::{Label, Leaf, Record, MODULE_VALUE};
fn read<T: Record<Output = u32>>(value: &T) -> u32 { value.value() + T::ID }
fn main() {
    let value = Leaf(5);
    assert_eq!(read(&value), 39);
    assert_eq!(<Leaf as Record>::ID, 17);
    assert_eq!(Leaf::WIDTH, 4);
    assert_eq!(MODULE_VALUE, 41);
    let object: &dyn Label = &value;
    assert_eq!(object.label(), 8);
    let method: fn(&Leaf) -> u32 = <Leaf as Record>::value;
    assert_eq!(method(&value), 22);
}
