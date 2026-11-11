use std::ops::RangeInclusive;

fn slot(value: &mut u16, range: RangeInclusive<u16>) -> &mut u16 {
    assert_eq!((*range.start(), *range.end()), (1, 3));
    value
}

struct State {
    last: u16,
}

fn main() {
    let mut first = 7u16;
    let mut tail = 11u16;
    let mut state = State { last: 13 };

    (*slot(&mut first, 1..=3), .., tail) = (17, false, (), 19);
    assert_eq!((first, tail), (17, 19));

    [first, .., state.last] = [23, 29, 31, 37];
    assert_eq!((first, state.last), (23, 37));

    (*slot(&mut first, 1..=3), [state.last, .., tail]) = (41, [43, 47, 53]);
    assert_eq!((first, state.last, tail), (41, 43, 53));
}
