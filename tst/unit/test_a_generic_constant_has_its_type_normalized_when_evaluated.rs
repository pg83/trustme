// icu_normalizer's `const EMPTY_CHAR: &ZeroSlice<char> = zeroslice![];`
// evaluates `ZeroSlice::<char>::new_empty()`, whose `&[]` is lifted to a
// constant of type `[<T as AsULE>::ULE; 0]`. Upstream's evaluator takes an
// item's type instantiated and normalized (`instantiate_and_normalize_erasing_regions`);
// we instantiated it only and asked for the layout of the projection.
pub trait AsULE { type ULE: Copy; }
impl AsULE for char { type ULE = [u8; 3]; }
impl AsULE for u16 { type ULE = [u8; 2]; }
#[repr(transparent)]
pub struct ZeroSlice<T: AsULE>([T::ULE]);
impl<T: AsULE> ZeroSlice<T> {
    pub const fn new_empty() -> &'static Self { Self::from_ule_slice(&[]) }
    pub const fn from_ule_slice(slice: &[T::ULE]) -> &Self {
        unsafe { core::mem::transmute(slice) }
    }
    pub const fn len(&self) -> usize { self.0.len() }
}
const EMPTY_U16: &ZeroSlice<u16> = ZeroSlice::new_empty();
const EMPTY_CHAR: &ZeroSlice<char> = ZeroSlice::new_empty();
fn main() {
    assert_eq!(EMPTY_U16.len(), 0);
    assert_eq!(EMPTY_CHAR.len(), 0);
}
