trait Observe {
    fn peek(&self) -> u16;
}
impl Observe for u16 {
    fn peek(&self) -> u16 { *self }
}

#[derive(Clone, Copy)]
union Carry<'a, T: Copy + Observe + 'a, const N: usize> {
    reference: &'a T,
    bytes: [u8; N],
}

fn duplicate<'a, T: Copy + Observe + 'a, const N: usize>(
    value: Carry<'a, T, N>,
) -> Carry<'a, T, N> {
    value.clone()
}

fn main() {
    let number = 181u16;
    let original = Carry::<u16, 5> { reference: &number };
    let copied = duplicate(original);
    assert_eq!(unsafe { copied.reference }.peek(), 181);
    assert_eq!(unsafe { original.reference }.peek(), 181);
}
