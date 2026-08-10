// Extracted from src/expressions/loop-expr.md:320
#![allow(unused)]
fn main() {
    fn do_thing() {}
    fn condition_not_met() -> bool { true }
    fn do_next_thing() {}
    fn do_last_thing() {}
    let result = 'block: {
        do_thing();
        if condition_not_met() {
            break 'block 1;
        }
        do_next_thing();
        if condition_not_met() {
            break 'block 2;
        }
        do_last_thing();
        3
    };
}
