// crypto-bigint converts `&[u8; 16]` with `EncodedUint::from(bytes)`. Among the
// `From` candidates is `From<&[u8; BYTES]> for EncodedUint<LIMBS> where
// [u8; BYTES]: EncodedSize<Target = EncodedUint<LIMBS>>`; upstream instantiates
// `BYTES` with a fresh variable, and the nested goal `[u8; ?B]: EncodedSize`
// meets `impl EncodedSize for [u8; 16]`. Binding the candidate's parameters
// here kept only the name and index of a const parameter, so `BYTES` came back
// as a placeholder no instantiation owned, the nested goal failed against
// `[u8; 16]`, and the reflexive `From<T> for T` was picked instead.
use std::convert::TryInto;

pub trait EncodedSize {
    type Target;
}

#[derive(Debug, PartialEq)]
pub struct EncodedUint<const LIMBS: usize>([u64; LIMBS]);

impl EncodedSize for [u8; 16] {
    type Target = EncodedUint<2>;
}

impl EncodedSize for EncodedUint<2> {
    type Target = [u8; 16];
}

impl<const LIMBS: usize> EncodedUint<LIMBS> {
    fn to_bytes(&self) -> Vec<u8> {
        self.0.iter().flat_map(|limb| limb.to_le_bytes()).collect()
    }
}

impl<const BYTES: usize, const LIMBS: usize> From<[u8; BYTES]> for EncodedUint<LIMBS>
where
    [u8; BYTES]: EncodedSize<Target = EncodedUint<LIMBS>>,
{
    fn from(input: [u8; BYTES]) -> Self {
        let mut out = EncodedUint([0u64; LIMBS]);
        for (i, chunk) in input.chunks(8).enumerate() {
            out.0[i] = u64::from_le_bytes(chunk.try_into().unwrap());
        }
        out
    }
}

impl<const BYTES: usize, const LIMBS: usize> From<&[u8; BYTES]> for EncodedUint<LIMBS>
where
    [u8; BYTES]: EncodedSize<Target = EncodedUint<LIMBS>>,
{
    fn from(input: &[u8; BYTES]) -> Self {
        Self::from(*input)
    }
}

impl<const BYTES: usize, const LIMBS: usize> From<EncodedUint<LIMBS>> for [u8; BYTES]
where
    EncodedUint<LIMBS>: EncodedSize<Target = [u8; BYTES]>,
{
    fn from(input: EncodedUint<LIMBS>) -> Self {
        let mut out = [0u8; BYTES];
        out.copy_from_slice(&input.to_bytes());
        out
    }
}

fn main() {
    let bytes = b"0011223344556677";
    let n = EncodedUint::from(bytes);
    assert_eq!(n.to_bytes(), bytes);
    let n = EncodedUint::from(*bytes);
    assert_eq!(n.to_bytes(), bytes);
    let n: [u8; 16] = EncodedUint::from(bytes).into();
    assert_eq!(&n, bytes);
}
