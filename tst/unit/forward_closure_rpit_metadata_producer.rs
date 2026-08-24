#![crate_type = "rlib"]

pub struct LookupResult<T>(pub T);

pub struct SimpleLookup<T, F> {
    pub value: T,
    pub callback: F,
}

fn complete<T, F>(value: T) -> LookupResult<SimpleLookup<T, F>>
where
    F: FnOnce() -> T,
{
    let _ = value;
    loop {}
}

pub fn make<T: Copy>(value: T) -> LookupResult<SimpleLookup<T, impl FnOnce() -> T>> {
    let complete = |value| complete(value);
    if false {
        return complete(value);
    }

    LookupResult(SimpleLookup {
        value,
        callback: move || value,
    })
}
