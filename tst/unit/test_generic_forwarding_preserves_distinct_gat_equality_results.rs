trait Family {
    type Member<T>;
}

struct Identity;

impl Family for Identity {
    type Member<T> = T;
}

fn second<F>(value: F::Member<u32>) -> u32
where
    F: Family<Member<u32> = u32>,
{
    value
}

fn forward<F>(value: F::Member<u32>) -> u32
where
    F: Family<Member<u8> = u8>,
    F: Family<Member<u32> = u32>,
{
    second::<F>(value)
}

fn main() {
    assert_eq!(forward::<Identity>(83), 83);
}
