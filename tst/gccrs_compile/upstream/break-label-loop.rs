// `break 'label loop {}` used to ICE in the code generator because the
// labeled loop never registered its result temporary against the label.

pub fn foo() {
    'a: loop {
        break 'a loop {};
    }
}
