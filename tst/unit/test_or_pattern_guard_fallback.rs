struct Pair {
    left: i32,
    right: i32,
}

fn tuple_guard_retries_next_alternative() {
    let mut guards = 0;
    let value = ((1, 2), 3);
    let selected = match value {
        ((x, _), _) | ((_, x), _)
            if {
                guards += 1;
                x == 2
            } => x,
        _ => -1,
    };

    assert_eq!(selected, 2);
    assert_eq!(guards, 2);
}

fn slice_guard_retries_next_alternative() {
    let mut guards = 0;
    let value = [1, 2, 3];
    let selected = match &value[..] {
        [x, ..] | [_, x, ..]
            if {
                guards += 1;
                *x == 2
            } => *x,
        _ => -1,
    };

    assert_eq!(selected, 2);
    assert_eq!(guards, 2);
}

fn struct_guard_retries_next_alternative() {
    let mut guards = 0;
    let value = Pair { left: 1, right: 2 };
    let selected = match value {
        Pair { left: x, .. } | Pair { right: x, .. }
            if {
                guards += 1;
                x == 2
            } => x,
        _ => -1,
    };

    assert_eq!(selected, 2);
    assert_eq!(guards, 2);
}

fn cartesian_alternatives_are_tried_left_to_right() {
    let mut guards = 0;
    let value = ((false, true), (false, true));
    let selected = match value {
        ((a, _) | (_, a), (b, _) | (_, b))
            if {
                guards += 1;
                (a, b) == (false, true)
            } => (a, b),
        _ => (true, true),
    };

    assert_eq!(selected, (false, true));
    assert_eq!(guards, 2);
}

fn main() {
    tuple_guard_retries_next_alternative();
    slice_guard_retries_next_alternative();
    struct_guard_retries_next_alternative();
    cartesian_alternatives_are_tried_left_to_right();
}
