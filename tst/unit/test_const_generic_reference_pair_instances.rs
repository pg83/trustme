#![feature(adt_const_params, unsized_const_params)]
const LEFT: (&str, &str) = ("aa", "bb");
const RIGHT: (&str, &str) = ("aa", "cc");
fn pair<const VALUE: (&'static str, &'static str)>() -> (&'static str, &'static str) { VALUE }
fn main() {
    assert_eq!(pair::<LEFT>(), ("aa", "bb"));
    assert_eq!(pair::<RIGHT>(), ("aa", "cc"));
    assert_ne!(pair::<LEFT>(), pair::<RIGHT>());
}
