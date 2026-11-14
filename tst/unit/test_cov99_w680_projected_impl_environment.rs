trait Project {
    type Out;
}

struct Seed;

impl Project for Seed {
    type Out = u32;
}

trait Accept {}

impl<C, T> Accept for C
where
    C: Project<Out = T>,
    T: Copy,
{
}

fn need<C: Accept>(_: C) {}

fn main() {
    need(Seed);
    need(Seed);
    let value: <Seed as Project>::Out = 17;
    assert_eq!(value, 17);
}
