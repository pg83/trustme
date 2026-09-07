// The closure returned by `rule_to_parser` calls `rule_to_parser` itself, so its
// body names the opaque return type whose hidden type is that very closure.
// Upstream's closure type is nominal from the start (`ClosureArgs` over the
// parent's generics), so a body may mention its own closure type; the extraction
// here assigned a closure's path only after walking its body and met the
// reference to the still-active closure as a cycle.
//
// Same shape as the upstream test impl-trait/nested-hkl-lifetime.rs.
use std::iter::FromIterator;

struct DynamicAlt<P>(P);

impl<P> FromIterator<P> for DynamicAlt<P> {
    fn from_iter<T: IntoIterator<Item = P>>(_iter: T) -> Self {
        loop {}
    }
}

fn owned_context<I, F>(_: F) -> impl FnMut(I) -> I {
    |i| i
}

trait Parser<I> {}

impl<T, I> Parser<I> for T where T: FnMut(I) -> I {}

fn alt<I, P: Parser<I>>(_: DynamicAlt<P>) -> impl FnMut(I) -> I {
    |i| i
}

fn rule_to_parser<'c>() -> impl Parser<&'c str> {
    move |input| {
        let v: Vec<()> = vec![];
        alt(v.iter().map(|()| owned_context(rule_to_parser())).collect::<DynamicAlt<_>>())(input)
    }
}

fn main() {
    let _ = rule_to_parser;
}
