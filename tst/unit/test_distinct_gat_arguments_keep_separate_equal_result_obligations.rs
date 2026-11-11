trait Family {
    type Member<T>;
    fn small() -> Self::Member<u8>;
    fn large() -> Self::Member<u32>;
}

struct Numbers;

impl Family for Numbers {
    type Member<T> = u16;
    fn small() -> u16 { 47 }
    fn large() -> u16 { 59 }
}

fn both<F>(_: F) -> (u16, u16)
where
    F: Family<Member<u8> = u16>,
    F: Family<Member<u32> = u16>,
{
    (F::small(), F::large())
}

fn main() {
    assert_eq!(both(Numbers), (47, 59));
}
