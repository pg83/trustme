trait Cat {
    fn meow(&self) -> bool;
    fn purr(&self) -> isize {
        self.scratch() + 2
    }
    fn scratch(&self) -> isize;
}

impl Cat for isize {
    fn meow(&self) -> bool {
        true
    }
    fn scratch(&self) -> isize {
        3
    }
}

fn main() {
    assert!(5.meow());
    assert_eq!(3.purr(), 5);
}
