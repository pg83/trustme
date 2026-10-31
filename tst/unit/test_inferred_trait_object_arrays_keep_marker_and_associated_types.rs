trait Value { type Item; fn value(&self) -> Self::Item; }
impl Value for u16 { type Item = u32; fn value(&self) -> u32 { *self as u32 + 1 } }
fn first<T>(values: &mut [&mut (dyn Value<Item = T> + Send + Sync)]) -> T { values[0].value() }
fn main() {
    let mut left = 17u16;
    let mut right = 19u16;
    let mut values: [&mut (dyn Value<Item = _> + Sync + Send); 2] = [&mut left, &mut right];
    assert_eq!(first(&mut values), 18u32);
    values.swap(0, 1);
    assert_eq!(first(&mut values), 20u32);
}
