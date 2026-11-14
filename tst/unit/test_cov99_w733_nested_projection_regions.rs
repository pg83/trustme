//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Item {
    type Out;
}

trait Family<F> {
    type Inner: Item;
}

trait Output {
    type Value;
}

impl<T: Family<F>, F> Output for (T, F) {
    type Value = <<T as Family<F>>::Inner as Item>::Out;
}

fn require<P: Output<Value = V>, V>() {}

fn equal<'a, 'b, T>()
where
    'a: 'b,
    'b: 'a,
    T: Family<fn(&'a ())> + Family<fn(&'b ())>,
{
    require::<(T, fn(&'a ())), <<T as Family<fn(&'b ())>>::Inner as Item>::Out>();
}
