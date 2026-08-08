struct Pair {
    first: u32,
    second: u32,
}

fn exits_from_condition() -> u32 {
    if (return 10) {}
    0
}

fn exits_from_match_scrutinee() -> u32 {
    match return 11 {
        _ => 0,
    }
}

fn exits_from_bool_left() -> bool {
    (return true) || false
}

fn exits_from_call_argument() -> u32 {
    core::convert::identity(return 12)
}

fn exits_from_tuple() -> u32 {
    let _ = (0, return 13);
    0
}

fn exits_from_struct_field() -> Result<Pair, u32> {
    Ok(Pair {
        first: 1,
        second: return Err(14),
    })
}

fn exits_loop_from_condition() -> u32 {
    loop {
        if (break 15) {}
    }
}

fn exits_from_cloned_guard(value: u32) -> u32 {
    match value {
        0 | 1 if return 16 => 0,
        _ => 17,
    }
}

fn main() {
    assert_eq!(exits_from_condition(), 10);
    assert_eq!(exits_from_match_scrutinee(), 11);
    assert!(exits_from_bool_left());
    assert_eq!(exits_from_call_argument(), 12);
    assert_eq!(exits_from_tuple(), 13);
    assert!(matches!(exits_from_struct_field(), Err(14)));
    assert_eq!(exits_loop_from_condition(), 15);
    assert_eq!(exits_from_cloned_guard(1), 16);
    assert_eq!(exits_from_cloned_guard(2), 17);
}
