// num-modular's `impl ModularCoreOps<BigUint, &BigUint> for &BigUint` forwards
// with `self.addm(&rhs, &m)`, `m: &BigUint`. Two impls for `&BigUint` differ
// in `Rhs`; once the first argument has fixed it, one impl alone fixes
// `Modulus`. rustc selects the pending obligations before it coerces each
// argument (`coerce` resolves its target with obligations), so the second
// argument meets `&BigUint` and reaches it by dereferencing `&&BigUint`. We
// selected once before all arguments, read `Modulus` off the second argument
// as `&&BigUint`, and reported "No applicable methods".
pub trait Ops<Rhs = Self, M = Self> {
    type Output;
    fn addm(self, rhs: Rhs, m: M) -> Self::Output;
}
pub struct Big(pub u32);
impl Ops<&Big, &Big> for &Big {
    type Output = Big;
    fn addm(self, rhs: &Big, m: &Big) -> Big {
        Big((self.0 + rhs.0) % m.0)
    }
}
impl Ops<Big, &Big> for &Big {
    type Output = Big;
    fn addm(self, rhs: Big, m: &Big) -> Big {
        self.addm(&rhs, &m)
    }
}
impl Ops<&Big, &Big> for Big {
    type Output = Big;
    fn addm(self, rhs: &Big, m: &Big) -> Big {
        (&self).addm(rhs, &m)
    }
}
impl Ops<Big, &Big> for Big {
    type Output = Big;
    fn addm(self, rhs: Big, m: &Big) -> Big {
        (&self).addm(&rhs, &m)
    }
}
fn main() {
    assert_eq!(Big(5).addm(Big(4), &Big(7)).0, 2);
    assert_eq!((&Big(5)).addm(Big(4), &Big(7)).0, 2);
    assert_eq!(Big(6).addm(&Big(4), &Big(7)).0, 3);
}
