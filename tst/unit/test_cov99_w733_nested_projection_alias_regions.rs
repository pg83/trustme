//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

type Unit = ();

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

fn equal<T: Family<for<'a> fn(&'a ())>>() {
    require::<
        (T, for<'a> fn(&'a ())),
        <<T as Family<for<'a> fn(&'a Unit)>>::Inner as Item>::Out,
    >();
}
