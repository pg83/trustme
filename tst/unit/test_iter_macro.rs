// compile-flags: -Zvalidate-mir
#![feature(iter_macro, yield_expr)]

use std::iter::iter;

fn main() {
    let captured = String::from("abc");
    let make = iter! { |offset| {
        yield captured.len() + offset;
        yield offset;
    }};

    let mut first = make(1);
    let mut second = make(4);
    assert_eq!(first.next(), Some(4));
    assert_eq!(first.next(), Some(1));
    assert_eq!(first.next(), None);
    assert_eq!(first.next(), None);
    assert_eq!(second.next(), Some(7));
    assert_eq!(second.next(), Some(4));
    assert_eq!(second.next(), None);

    let with_owned_arg = iter! { |text: String| {
        yield text;
    }};
    let mut owned_arg = with_owned_arg(String::from("argument"));
    assert_eq!(owned_arg.next().as_deref(), Some("argument"));
    assert_eq!(owned_arg.next(), None);

    let explicit_return = iter! { || -> () {
        yield 9;
    }};
    let mut explicit_return = explicit_return();
    assert_eq!(explicit_return.next(), Some(9));
    assert_eq!(explicit_return.next(), None);

    let once = {
        let owned = String::from("owned");
        iter! { move || {
            yield owned.len();
        }}
    };
    assert_eq!(once().next(), Some(5));
}
