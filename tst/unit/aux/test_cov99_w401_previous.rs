pub use test_cov99_w401_parent::Parent as OldParent;

#[inline]
pub fn read<T: OldParent<1> + ?Sized>(value: &T) -> u32 {
    <T as OldParent<1>>::read(value)
}
