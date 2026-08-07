// Extracted from library/core/src/iter/traits/iterator.rs:2068
#![allow(unused)]
#![feature(iterator_try_collect)]
fn main() {

    let u: Vec<Result<i32, ()>> = vec![Ok(1), Ok(2), Ok(3)];
    let v = u.into_iter().try_collect::<Vec<i32>>();
    assert_eq!(v, Ok(vec![1, 2, 3]));

    let u = vec![Ok(1), Ok(2), Err(()), Ok(3)];
    let v = u.into_iter().try_collect::<Vec<i32>>();
    assert_eq!(v, Err(()));
}
