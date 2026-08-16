// Nothing inhabits `!`, so a match on a diverging scrutinee never runs any arm
// and the patterns have no type to disagree with. They were checked against `!`
// anyway and rejected as unable to match it.
//
// Same shape as the upstream tests binding/match-bot-2.rs and
// issues/issue-30371.rs.
#![allow(unreachable_code)]

fn intArm() -> isize {
    match return 1 {
        2 => 3,
        _ => panic!(),
    }
}

fn tupleArm() -> u32 {
    match return 4 {
        () => 5,
    }
}

fn structuredArm() -> u32 {
    // A structured pattern against `!` is equally unreachable.
    match return 6 {
        (7u32, ()) => 8,
        _ => 9,
    }
}

fn diverges(v: u32) -> u32 {
    // The scrutinee is a `!`-typed expression that is not a `return`.
    let n = match panic!("never") {
        Some(x) => x,
        None => v,
    };
    n
}

fn main() {
    assert_eq!(intArm(), 1);
    assert_eq!(tupleArm(), 4);
    assert_eq!(structuredArm(), 6);

    // `diverges` would panic, so it is only checked for compiling.
    let f: fn(u32) -> u32 = diverges;
    assert!(f as usize != 0);
}
