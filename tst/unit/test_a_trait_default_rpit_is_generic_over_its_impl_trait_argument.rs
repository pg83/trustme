// utoipa-gen 6: a trait's default method `fn validate_attributes<'a, I>(..,
// validate: impl Fn(&Attribute) -> .. + 'a) -> impl Iterator<..>`. The
// associated type that stands for the returned `impl Iterator` is generic
// over all of the method's parameters, the one `impl Fn` brings included,
// as rustc's synthesized RPITIT type takes the function's generics.
trait Response {
    fn keep<'a, I: IntoIterator<Item = &'a u8>>(items: I, check: impl Fn(&u8) -> bool + 'a) -> impl Iterator<Item = u8> {
        items.into_iter().filter_map(move |x| if check(x) { Some(*x) } else { None })
    }
}

struct Named;

impl Response for Named {}

impl Named {
    fn run(values: &[u8]) -> Vec<u8> {
        Self::keep(values, |x| *x > 1).chain(Self::keep(values, |x| *x == 1)).collect()
    }
}

fn main() {
    assert_eq!(Named::run(&[1, 2, 3]), vec![2, 3, 1]);
}
