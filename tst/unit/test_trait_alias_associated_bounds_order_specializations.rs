#![feature(specialization, trait_alias)]
#![allow(incomplete_features)]
trait Provider { type Item; }
trait CloneFamily = Provider<Item: Clone>;
trait CopyFamily = Provider<Item: Copy>;
struct CopyOwner;
impl Provider for CopyOwner { type Item = u16; }
struct CloneOwner;
impl Provider for CloneOwner { type Item = String; }
struct Wrap<T>(T);
trait Rank { fn rank() -> u8; }
impl<T: CloneFamily> Rank for Wrap<T> { default fn rank() -> u8 { 1 } }
impl<T: CopyFamily> Rank for Wrap<T> { fn rank() -> u8 { 2 } }
fn main() {
    assert_eq!(Wrap::<CopyOwner>::rank(), 2);
    assert_eq!(Wrap::<CloneOwner>::rank(), 1);
}
