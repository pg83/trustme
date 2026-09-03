// A non-capturing closure and a function pointer join at the function
// pointer: the pointer is the upper bound of that coercion lattice, so a
// `match` whose arms mix the two has the pointer as its result type, and the
// closure's own signature - including a return that is just a literal, with
// nothing else to pin it - is taken from that join. A diverging arm is the
// bottom of the lattice and must not disturb the join, whichever arm order
// the match is written in.

fn foo(x: usize) -> usize {
    x + 1
}

type FnPointer = fn(usize) -> usize;

fn main() {
    let pointer_first = match 0 {
        0 => foo as FnPointer,
        2 => |_a| 2,
        _ => unimplemented!(),
    };
    assert_eq!(pointer_first(3), 4);

    let closure_first = match 1 {
        2 => |_a| 2,
        1 => foo as FnPointer,
        _ => unimplemented!(),
    };
    assert_eq!(closure_first(3), 4);

    let closure_chosen = match 2 {
        2 => |_a| 2,
        0 => foo as FnPointer,
        _ => unimplemented!(),
    };
    assert_eq!(closure_chosen(3), 2);
}
