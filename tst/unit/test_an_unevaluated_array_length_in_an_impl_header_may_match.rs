// crypto-common implements `SerializableState for [u16; sizes::U1::USIZE]`
// and reads `SerializedState::<Self>::default()`, an `Array<u8, <[u16; 1] as
// SerializableState>::SerializedStateSize>`. The impl's array length is an
// associated constant, still unevaluated in the impl's header. Upstream's
// fast impl filter lets an unevaluated constant through (`consts_may_unify`)
// and the relation of the heads evaluates it; the filter here called `1` and
// the unevaluated length unequal, no impl normalized the projection, and
// `Default` for the array type was not found.
pub trait Unsigned {
    const USIZE: usize;
}

pub struct U1;
pub struct U2;

impl Unsigned for U1 {
    const USIZE: usize = 1;
}

impl Unsigned for U2 {
    const USIZE: usize = 2;
}

pub trait Sizes {
    type Size: Unsigned;
}

pub struct Array<T, N: Unsigned>(Vec<T>, std::marker::PhantomData<N>);

impl<T: Default + Clone, N: Unsigned> Default for Array<T, N> {
    fn default() -> Self {
        Array(vec![T::default(); N::USIZE], std::marker::PhantomData)
    }
}

pub type SerializedState<T> = Array<u8, <T as Sizes>::Size>;

impl Sizes for [u16; U1::USIZE] {
    type Size = U2;
}

impl Sizes for [u16; U2::USIZE] {
    type Size = U1;
}

fn serialize() -> usize {
    let state = SerializedState::<[u16; U1::USIZE]>::default();
    state.0.len()
}

fn main() {
    assert_eq!(serialize(), 2);
}
