enum Choice<const N: usize> {
    Value,
}

fn accept<const N: usize>(_: Choice<N>) {}

fn main() {
    accept(Choice::<1>::Value);
    accept(Choice::Value::<2>);

    match Choice::<3>::Value {
        Choice::<3>::Value => {}
    }
}
