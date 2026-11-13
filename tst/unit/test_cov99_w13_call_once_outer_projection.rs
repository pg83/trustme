//@ env: RUSTC_BOOTSTRAP=1
#![feature(fn_traits)]

trait Link<T> {}
impl<T> Link<T> for () {}
trait Forward<T> {
    type Value;
}
impl<T> Forward<T> for ()
where
    (): Link<T>,
{
    type Value = (u8, T);
}

fn projected<I: Iterator>(mut input: I) -> <() as Forward<I::Item>>::Value {
    (9, input.next().unwrap())
}

fn main() {
    let value: (u8, _) = projected::<_>.call_once((
        [5u16].into_iter()
            .filter_map(|value| value.into())
            .filter_map(|value| value.into()),
    ));
    assert_eq!(value, (9, 5));
}
