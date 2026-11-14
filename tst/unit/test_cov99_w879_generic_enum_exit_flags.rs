//@ compile-flags: -Z dump-mir -Z mir-opt-level=1

enum Value<T> {
    Pair(T, T),
    Empty,
}

#[inline(never)]
fn partial_exit<T>(value: Value<T>, first_exit: bool, initialize: bool) {
    let pending;
    if first_exit {
        if initialize {
            pending = value;
            match pending {
                Value::Pair(first, _) => drop(first),
                Value::Empty => (),
            }
        }
        return;
    }
    if initialize {
        pending = value;
        match pending {
            Value::Pair(first, _) => drop(first),
            Value::Empty => (),
        }
    }
}

fn main() {
    partial_exit(Value::Pair(1u8, 2u8), true, true);
    partial_exit(Value::Pair(3u8, 4u8), false, true);
    partial_exit(Value::Pair(5u8, 6u8), true, false);
    partial_exit(Value::Pair(7u8, 8u8), false, false);
    partial_exit(Value::<u8>::Empty, true, true);
    partial_exit(Value::<u8>::Empty, false, true);
}
