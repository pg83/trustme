// alloy-primitives' `wrap_fixed_bytes!` has derive_more implement
// `IntoIterator for Address` through `FixedBytes<20>`: the impl's own bound
// and items name a const argument that is an anonymous constant. Upstream
// gives such a constant no generics at all (`generics_of`, `AnonConstKind::MCG`
// under min_const_generics), so evaluating it never asks for the impl's
// where-clauses. We built its body in the impl's environment: preparing that
// environment normalized the bound, the bound evaluated the constant, and the
// constant prepared the environment again until the stack ran out.
pub struct FixedBytes<const N: usize>(pub [u8; N]);

impl<const N: usize> IntoIterator for FixedBytes<N> {
    type Item = u8;
    type IntoIter = core::array::IntoIter<u8, N>;
    fn into_iter(self) -> Self::IntoIter {
        self.0.into_iter()
    }
}

pub struct Address(pub FixedBytes<{ 4 * 5 }>);

impl IntoIterator for Address
where
    FixedBytes<{ 4 * 5 }>: IntoIterator,
{
    type Item = <FixedBytes<{ 4 * 5 }> as IntoIterator>::Item;
    type IntoIter = <FixedBytes<{ 4 * 5 }> as IntoIterator>::IntoIter;
    fn into_iter(self) -> Self::IntoIter {
        self.0.into_iter()
    }
}

fn main() {
    let a = Address(FixedBytes([7; 20]));
    assert_eq!(a.into_iter().map(u32::from).sum::<u32>(), 140);
}
