#![feature(trait_alias)]

trait ComparableTo<T> = Send where T: PartialEq<Self>;
trait NestedComparableTo<T> = ComparableTo<T>;

fn equals(value: &impl ComparableTo<i32>) -> bool {
    22_i32 == *value
}

fn nested_equals(value: &impl NestedComparableTo<i32>) -> bool {
    22_i32 == *value
}

fn main() {
    assert!(equals(&22));
    assert!(nested_equals(&22));
}
