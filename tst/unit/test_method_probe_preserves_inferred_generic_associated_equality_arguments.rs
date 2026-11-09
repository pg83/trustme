trait Family {
    type Member<T>;
}

struct Seed;

impl Family for Seed {
    type Member<T> = u16;
}

struct Broker;

impl Broker {
    fn pick<F, U>(&self, value: F::Member<U>, _family: F, _argument: U) -> u16
    where
        F: Family<Member<U> = u16>,
    {
        value
    }
}

fn main() {
    assert_eq!(Broker.pick(37u16, Seed, 5u8), 37);
}
