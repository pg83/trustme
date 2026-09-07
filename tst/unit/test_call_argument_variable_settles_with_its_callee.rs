// `f(&inputs)` on `f: &Box<dyn Fn(&[i32]) -> i32>`: the call's argument variable
// takes the parameter type from the callee's signature, and the argument then
// coerces to it (upstream `check_call` resolves the callee first, then
// `check_argument_types` coerces each argument to its formal type).  When the
// callee's own type still waits on a finalisation effect, the argument variable
// belongs to the same pending call and must wait with it: settling it from the
// argument alone asks for `Fn(&Vec<i32>)`, which the object does not implement.
//
// Reduced from the exercism `react` solution.
struct Cell {
    f: Box<dyn Fn(&[i32]) -> i32>,
    deps: Vec<usize>,
}

fn compute(cell: &Cell, values: &[i32]) -> i32 {
    let (deps, f) = (&cell.deps, &cell.f);
    let inputs: Vec<_> = deps.iter().map(|&i| values[i]).collect();
    f(&inputs)
}

fn main() {
    let cell = Cell { f: Box::new(|xs| xs.iter().sum()), deps: vec![0, 2] };
    assert_eq!(compute(&cell, &[1, 2, 3]), 4);
}
