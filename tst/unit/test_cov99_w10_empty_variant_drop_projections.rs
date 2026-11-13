static mut DROPS: u32 = 0;

#[derive(Clone)]
#[repr(align(16))]
struct Marker;

impl Drop for Marker {
    fn drop(&mut self) {
        unsafe { DROPS += 1; }
    }
}

enum Choice {
    Empty,
    Tuple(),
    Named {},
    One(Marker),
}

fn inspect(choice: &Choice) -> u8 {
    match choice {
        Choice::Empty => 0,
        Choice::Tuple() => 1,
        Choice::Named {} => 2,
        Choice::One(Marker {}) => 3,
    }
}

fn main() {
    assert_eq!(inspect(&Choice::Empty), 0);
    assert_eq!(inspect(&Choice::Tuple()), 1);
    assert_eq!(inspect(&Choice::Named {}), 2);
    let choice = Choice::One(Marker);
    assert_eq!(inspect(&choice), 3);
    drop(choice);
    let original = Marker;
    let cloned = original.clone();
    drop(cloned);
    drop(original);
    drop(Box::new(Marker));
    assert_eq!(unsafe { DROPS }, 4);
}
