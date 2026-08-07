// Extracted from library/core/src/iter/traits/double_ended.rs:75
#![allow(unused)]
fn main() {
    let vec = vec![(1, 'a'), (1, 'b'), (1, 'c'), (2, 'a'), (2, 'b')];
    let uniq_by_fst_comp = || {
        let mut seen = std::collections::HashSet::new();
        vec.iter().copied().filter(move |x| seen.insert(x.0))
    };

    assert_eq!(uniq_by_fst_comp().last(), Some((2, 'a')));
    assert_eq!(uniq_by_fst_comp().next_back(), Some((2, 'b')));

    assert_eq!(
        uniq_by_fst_comp().fold(vec![], |mut v, x| {v.push(x); v}),
        vec![(1, 'a'), (2, 'a')]
    );
    assert_eq!(
        uniq_by_fst_comp().rfold(vec![], |mut v, x| {v.push(x); v}),
        vec![(2, 'b'), (1, 'c')]
    );
}
