// `impl ConstDefault` names the type with its const parameter defaulted. The
// default is stored as an unevaluated expression carrying the item's own
// generic arguments, and substituting into it while those arguments were still
// being filled asserted with "Value param N out of range".
pub struct ConstDefault<const N: usize = 3>;

impl<const N: usize> ConstDefault<N> {
    fn foo(self) -> usize {
        N
    }
}

impl ConstDefault {
    fn new() -> Self {
        ConstDefault
    }
}

pub fn main() {
    let s = ConstDefault::new();
    assert_eq!(s.foo(), 3);
}
