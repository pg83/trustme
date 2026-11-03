struct Holder<T>(T);
impl<T> Holder<T> {
    fn inline<U: ?Sized + AsRef<str>>(&self, value: &U) -> usize { value.as_ref().len() }
    fn clause<U>(&self, value: &U) -> usize where U: ?Sized + AsRef<str> { value.as_ref().len() }
}
fn main() {
    let holder = Holder(53u32);
    assert_eq!(holder.inline("inline"), 6);
    assert_eq!(holder.clause("where"), 5);
    assert_eq!(holder.0, 53);
}
