//@ compile-fail: Type mismatch between u32 and
trait Family {
    type Member<T>;
}

fn missing<F>(value: F::Member<u32>) -> u32
where
    F: Family<Member<u8> = u32>,
{
    value
}

fn main() {}
