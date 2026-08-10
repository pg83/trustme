// Extracted from library/core/src/convert/mod.rs:157
#![allow(unused)]
fn main() {
    use core::ops::Deref;
    struct SomeType;
    impl Deref for SomeType {
        type Target = [u8];
        fn deref(&self) -> &[u8] {
            &[]
        }
    }
    impl<T> AsRef<T> for SomeType
    where
        T: ?Sized,
        <SomeType as Deref>::Target: AsRef<T>,
    {
        fn as_ref(&self) -> &T {
            self.deref().as_ref()
        }
    }
}
