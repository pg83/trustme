// `StateEntry::read(FontData::new(..))` with two traits in scope declaring `read`: one has no
// impl for `StateEntry<_>` (font-types' `Scalar`, `fn read(&[u8])`), the other has
// (`FontRead`, `fn read(FontData)`).  Resolving the path must probe the impls with the type's
// unknowns as unknowns and skip the trait that cannot apply, not take the first trait in
// scope with the name and then mismatch the argument.  (read-fonts `tables/aat.rs:480`)
pub struct FontData<'a> {
    bytes: &'a [u8],
}

impl<'a> FontData<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        FontData { bytes }
    }
}

pub trait Scalar: Sized {
    fn read(bytes: &[u8]) -> Option<Self>;
}

impl Scalar for u16 {
    fn read(bytes: &[u8]) -> Option<u16> {
        Some(u16::from_be_bytes([*bytes.first()?, *bytes.get(1)?]))
    }
}

pub trait FontRead<'a>: Sized {
    fn read(data: FontData<'a>) -> Result<Self, ()>;
}

pub trait FixedSize {
    const RAW_BYTE_LEN: usize;
}

impl FixedSize for u16 {
    const RAW_BYTE_LEN: usize = 2;
}

pub struct StateEntry<T = ()> {
    pub new_state: u16,
    pub payload: T,
}

impl<'a, T: Scalar + FixedSize> FontRead<'a> for StateEntry<T> {
    fn read(data: FontData<'a>) -> Result<Self, ()> {
        let new_state = u16::read(data.bytes).ok_or(())?;
        let payload = T::read(data.bytes.get(2..).ok_or(())?).ok_or(())?;
        Ok(StateEntry { new_state, payload })
    }
}

mod scalar_last {
    use super::{FontData, FontRead, Scalar, StateEntry};

    pub fn entry(bytes: &[u8]) -> Result<StateEntry<u16>, ()> {
        StateEntry::read(FontData::new(bytes))
    }
}

mod scalar_first {
    use super::{FontData, FontRead, StateEntry};
    use super::Scalar;

    pub fn entry(bytes: &[u8]) -> Result<StateEntry<u16>, ()> {
        StateEntry::read(FontData::new(bytes))
    }
}

fn main() {
    let bytes = [0, 3, 0, 7];
    let a = scalar_last::entry(&bytes).unwrap();
    let b = scalar_first::entry(&bytes).unwrap();
    assert_eq!((a.new_state, a.payload), (3, 7));
    assert_eq!((b.new_state, b.payload), (3, 7));
}
