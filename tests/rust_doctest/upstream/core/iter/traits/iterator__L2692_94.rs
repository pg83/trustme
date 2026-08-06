// Extracted from library/core/src/iter/traits/iterator.rs:2692
#![allow(unused)]
#![feature(iterator_try_reduce)]
fn main() {
    
    let numbers = vec!["1", "2", "3", "4", "5"];
    let max: Result<Option<_>, <usize as std::str::FromStr>::Err> =
        numbers.into_iter().try_reduce(|x, y| {
            if x.parse::<usize>()? > y.parse::<usize>()? { Ok(x) } else { Ok(y) }
        });
    assert_eq!(max, Ok(Some("5")));
}
