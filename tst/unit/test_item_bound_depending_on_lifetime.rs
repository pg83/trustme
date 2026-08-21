//@ crate-type: lib
// A concrete-looking predicate that mentions the item's lifetime is an
// assumption of the item, not a trivial bound to prove at its declaration.

trait HasType {
    type Type;
}

struct Marker;

enum Conditional<'a>
where
    &'a Marker: HasType,
{
    Value(<&'a Marker as HasType>::Type),
}
