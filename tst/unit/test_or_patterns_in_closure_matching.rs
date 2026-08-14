fn main() {
    let make = |_| Some(1);
    let (|call| call) = match make(..) {
        |_| Some(2) => |_| Some(3),
        |_| _ => unreachable!(),
    };

    assert!(matches!(call(..), |_| Some(4)));
}
