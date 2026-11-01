enum Event { Left(u16), Right(u16), Empty }

fn read(event: &Event) -> u16 {
    match event {
        Event::Left(value) | Event::Right(value) => *value,
        Event::Empty => 0,
    }
}

fn change(event: &mut Event) {
    match event {
        Event::Left(value) | Event::Right(value) => *value += 7,
        Event::Empty => {},
    }
}

fn main() {
    let mut left = Event::Left(11);
    let mut right = Event::Right(17);
    change(&mut left);
    change(&mut right);
    assert_eq!(read(&left), 18);
    assert_eq!(read(&right), 24);
    assert_eq!(read(&Event::Empty), 0);
    let &(first, _) = &(23u16, 31u16);
    assert_eq!(first, 23);
    let &(Some(_) | None) = &None::<u16>;
}
