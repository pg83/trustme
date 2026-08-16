// A derived `Default` on an enum only ever builds the `#[default]` variant, so
// only that variant's field types need a bound. The derive bounded every type
// parameter, which made `MyOption::<NotDefault>::default()` unresolvable even
// though the default variant carries nothing.
//
// Same shape as the upstream test deriving/deriving-default-enum.rs.
#[derive(Debug, PartialEq)]
struct NotDefault;

#[derive(Default, Debug, PartialEq)]
enum MyOption<T> {
    #[default]
    None,
    #[allow(dead_code)]
    Some(T),
}

// A `#[default]` variant is always a unit variant, so an enum's derived
// `Default` never needs a bound on a type parameter.
#[derive(Default, Debug, PartialEq)]
enum Pair<A, B> {
    #[default]
    Neither,
    #[allow(dead_code)]
    Both(A, B),
}

#[derive(Default, Debug, PartialEq)]
enum Plain {
    #[default]
    Alpha,
    #[allow(dead_code)]
    Beta(NotDefault),
}

fn main() {
    assert_eq!(MyOption::<NotDefault>::default(), MyOption::None);
    assert_eq!(MyOption::<u32>::default(), MyOption::None);

    assert_eq!(Pair::<NotDefault, NotDefault>::default(), Pair::Neither);

    assert_eq!(Plain::default(), Plain::Alpha);
}
