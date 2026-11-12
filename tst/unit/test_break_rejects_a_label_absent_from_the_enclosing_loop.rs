//@ compile-fail: Could not find loop

fn main() {
    'outer: loop {
        break 'missing;
    }
}
