// Extracted from library/core/src/convert/mod.rs:266
#![allow(unused)]
fn main() {
    use core::ops::{Deref, DerefMut};
    struct SomeType;
    impl Deref for SomeType {
        type Target = [u8];
        fn deref(&self) -> &[u8] {
            &[]
        }
    }
    impl DerefMut for SomeType {
        fn deref_mut(&mut self) -> &mut [u8] {
            &mut []
        }
    }
    impl<T> AsMut<T> for SomeType
    where
        <SomeType as Deref>::Target: AsMut<T>,
    {
        fn as_mut(&mut self) -> &mut T {
            self.deref_mut().as_mut()
        }
    }
}
