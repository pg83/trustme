struct Pair<Input, Output>(Input, Output);

trait Project {
    type Output;
    fn project(self) -> Self::Output;
}

impl<Output> Project for Pair<u8, Output> {
    type Output = Output;
    fn project(self) -> Output { self.1 }
}

fn keep<T>(left: T, _: T) -> T { left }

fn main() {
    let input = 79;
    let output = 83;
    let selected = keep(input, <Pair<_, _> as Project>::project(Pair(input, output)));
    let selected: u8 = selected;
    assert_eq!(selected, 79);
}
