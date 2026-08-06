// Extracted from src/fn/closures/closure_examples/iter_find.md:59
fn main() {
    let vec = vec![1, 9, 3, 3, 13, 2];

    // `position` passes the iterator’s `Item` by value to the predicate.
    // `vec.iter()` yields `&i32`, so the predicate receives `&i32`,
    // which we pattern-match to dereference to `i32`.
    let index_of_first_even_number = vec.iter().position(|&x| x % 2 == 0);
    assert_eq!(index_of_first_even_number, Some(5));

    // `vec.into_iter()` yields `i32`, so the predicate receives `i32` directly.
    let index_of_first_negative_number = vec.into_iter().position(|x| x < 0);
    assert_eq!(index_of_first_negative_number, None);
}
