use std::ops::Index;
use std::panic::Location;

#[track_caller]
fn caller_line() -> u32 {
    Location::caller().line()
}

struct Values {
    before: u32,
}

impl Index<usize> for Values {
    type Output = ();

    fn index(&self, _: usize) -> &Self::Output {
        assert_eq!(caller_line(), self.before + 2);
        &()
    }
}

fn main() {
    let before = caller_line();
    (Values { before })
    [0];
}
