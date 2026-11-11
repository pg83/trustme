trait Family {
    type Member<T>;
}

struct Numbers(u32);

impl Family for Numbers {
    type Member<T> = u16;
}

fn constrained<F>(value: F) -> F
where
    F: Family<Member<u8> = u16>,
    F: Family<Member<u32> = u16>,
{
    value
}

fn main() {
    let value = constrained(Numbers(71));
    assert_eq!(value.0, 71);
}
