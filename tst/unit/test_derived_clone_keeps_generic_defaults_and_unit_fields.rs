#![feature(default_field_values)]

#[cfg(any())]
#[derive(0)]
struct Removed;

#[derive(Clone)]
struct Generic<T> { value: T, marker: () }

#[derive(Clone)]
struct Defaulted<T = u16> { value: Option<T> = None }

fn main() {
    let first = Generic { value: String::from("field"), marker: () };
    let second = first.clone();
    assert_eq!(first.value, "field");
    assert_eq!(second.value, "field");
    assert_eq!(second.marker, ());
    let defaulted: Defaulted = Defaulted { value: Some(42) };
    assert_eq!(defaulted.clone().value, Some(42));
    let empty: Defaulted<String> = Defaulted { .. };
    assert_eq!(empty.clone().value, None);
}
