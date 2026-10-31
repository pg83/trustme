trait Execute<F> { fn execute(steps: &[F], value: u16) -> u16; }
struct Runner;
impl Execute<fn(u16) -> u16> for Runner {
    fn execute(steps: &[fn(u16) -> u16], mut value: u16) -> u16 {
        for step in steps { value = step(value); }
        value
    }
}
fn run<F>(steps: &[F], value: u16) -> u16 where Runner: Execute<F> { Runner::execute(steps, value) }
fn add(value: u16) -> u16 { value + 2 }
fn multiply(value: u16) -> u16 { value * 3 }
fn main() { assert_eq!(run(&[add as fn(u16) -> u16, multiply, add], 3), 17); }
