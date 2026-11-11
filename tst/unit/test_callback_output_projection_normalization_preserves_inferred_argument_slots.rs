trait Produced {
    type Value;
}

impl<F, R> Produced for F
where
    F: FnOnce(u16) -> R,
{
    type Value = R;
}

struct Adapter<F>(F);

trait Projected {
    type Value;
}

impl<F: Produced> Projected for Adapter<F> {
    type Value = F::Value;
}

fn reenter<F>(callback: F, value: <Adapter<F> as Projected>::Value) -> <Adapter<F> as Projected>::Value
where
    Adapter<F>: Projected,
    F: FnOnce(<Adapter<F> as Projected>::Value) -> <Adapter<F> as Projected>::Value,
{
    callback(value)
}

fn main() {
    let mut calls = 0;
    let result = reenter(|value| { calls += 1; value }, 41u16);
    assert_eq!(result, 41u16);
    assert_eq!(calls, 1);
}
