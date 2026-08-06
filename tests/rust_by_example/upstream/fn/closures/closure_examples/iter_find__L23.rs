// Extracted from src/fn/closures/closure_examples/iter_find.md:23
fn main() {
    let vec1 = vec![1, 2, 3];
    let vec2 = vec![4, 5, 6];

    // `vec1.iter()` yields `&i32`.
    let mut iter = vec1.iter();
    // `vec2.into_iter()` yields `i32`.
    let mut into_iter = vec2.into_iter();

    // `iter()` yields `&i32`, and `find` passes `&Item` to the predicate.
    // Since `Item = &i32`, the closure argument has type `&&i32`,
    // which we pattern-match to dereference down to `i32`.
    println!("Find 2 in vec1: {:?}", iter.find(|&&x| x == 2));
    
    // `into_iter()` yields `i32`, and `find` passes `&Item` to the predicate.
    // Since `Item = i32`, the closure argument has type `&i32`,
    // which we pattern-match to dereference down to `i32`.
    println!("Find 2 in vec2: {:?}", into_iter.find(|&x| x == 2));

    let array1 = [1, 2, 3];
    let array2 = [4, 5, 6];

    // `array1.iter()` yields `&i32`, and `find` passes `&Item` to the
    // predicate. Since `Item = &i32`, the closure argument has type `&&i32`.
    println!("Find 2 in array1: {:?}", array1.iter().find(|&&x| x == 2));
    // `array2.into_iter()` yields `i32` (since Rust 2021 edition), and
    // `find` passes `&Item` to the predicate. Since `Item = i32`, the
    // closure argument has type `&i32`.
    println!("Find 2 in array2: {:?}", array2.into_iter().find(|&x| x == 2));
}
