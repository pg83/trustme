trait Meter<'a> {
    fn value(&self) -> u32;
}

impl<'a> Meter<'a> for u32 {
    fn value(&self) -> u32 {
        *self
    }
}

#[inline(never)]
fn free_method<'a>() -> fn(&(dyn Meter<'a> + 'a)) -> u32 {
    <(dyn Meter<'a> + 'a) as Meter<'a>>::value
}

#[inline(never)]
fn static_method() -> fn(&dyn Meter<'static>) -> u32 {
    <dyn Meter<'static> as Meter<'static>>::value
}

fn main() {
    let value = 37_u32;
    assert_eq!(free_method()(&value), 37);
    assert_eq!(static_method()(&value), 37);
}
