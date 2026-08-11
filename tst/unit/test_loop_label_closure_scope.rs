//@ compile-fail: Could not find loop label

fn main() {
    'outer: loop {
        let leave = || {
            break 'outer;
        };
        leave();
        break;
    }
}
