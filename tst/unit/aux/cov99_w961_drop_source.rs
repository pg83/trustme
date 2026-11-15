pub struct Owned<T>(pub T);

impl<T> Drop for Owned<T> {
    #[inline(never)]
    fn drop(&mut self) {}
}

#[inline(never)]
pub fn export_u8() {
    let _value = Owned(7u8);
}
