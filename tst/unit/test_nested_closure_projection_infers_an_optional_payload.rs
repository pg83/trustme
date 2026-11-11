trait Payload {
    type Value;
}

struct Optional<F>(F);

impl<F, T> Payload for Optional<F>
where
    F: Fn() -> Option<T>,
{
    type Value = T;
}

struct Batch<F>(F);

impl<F> Payload for Batch<F>
where
    Optional<F>: Payload,
{
    type Value = Vec<<Optional<F> as Payload>::Value>;
}

fn retain<F>(call: F) -> F
where
    Batch<F>: Payload<Value = Vec<u16>>,
{
    call
}

fn main() {
    let call = retain(|| Some(Default::default()));
    let value = call().unwrap();
    assert_eq!(std::mem::size_of_val(&value), 2);
    assert_eq!(value as u64, 0);
}
